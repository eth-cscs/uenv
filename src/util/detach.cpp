#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>

#include <fmt/format.h>

#include <util/detach.h>

namespace util {

expected<void, std::string> redirect_to_null(std::initializer_list<int> fds) {
    const int null = ::open("/dev/null", O_RDWR);
    if (null < 0) {
        return unexpected(
            fmt::format("opening /dev/null failed: {}", strerror(errno)));
    }
    // with a standard descriptor closed, open() can return one of `fds`
    bool keep = false;
    for (int fd : fds) {
        if (fd == null) {
            keep = true;
        } else if (::dup2(null, fd) < 0) {
            const auto msg = fmt::format("dup2(/dev/null, {}) failed: {}", fd,
                                         strerror(errno));
            ::close(null);
            return unexpected(msg);
        }
    }
    if (!keep) {
        ::close(null);
    }
    return {};
}

void close_fds_from(int first) {
    // close_range(2) is not wrapped by glibc before 2.34, and needs Linux 5.9
#ifdef SYS_close_range
    if (::syscall(SYS_close_range, static_cast<unsigned>(first), ~0u, 0u) ==
        0) {
        return;
    }
#endif
    for (long fd = first, max = ::sysconf(_SC_OPEN_MAX); fd < max; ++fd) {
        ::close(static_cast<int>(fd));
    }
}

void spawn_detached(const std::function<void()>& work,
                    std::chrono::seconds limit, detached_output output) {
    const pid_t child = ::fork();
    if (child < 0) {
        return;
    }
    if (child == 0) {
        // the intermediate child: a new session, then the grandchild, which is
        // reparented when this exits
        ::setsid();
        const pid_t grandchild = ::fork();
        if (grandchild != 0) {
            ::_exit(0);
        }
        // hold none of the caller's descriptors
        if (output == detached_output::keep) {
            if (!redirect_to_null({STDIN_FILENO})) {
                ::close(STDIN_FILENO);
            }
        } else if (!redirect_to_null(
                       {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})) {
            ::close(STDIN_FILENO);
            ::close(STDOUT_FILENO);
            ::close(STDERR_FILENO);
        }
        close_fds_from(STDERR_FILENO + 1);
        // SIGALRM ends the grandchild, whatever the caller did with it
        ::signal(SIGALRM, SIG_DFL);
        sigset_t alarm_set;
        ::sigemptyset(&alarm_set);
        ::sigaddset(&alarm_set, SIGALRM);
        ::sigprocmask(SIG_UNBLOCK, &alarm_set, nullptr);
        ::alarm(static_cast<unsigned>(limit.count()));
        try {
            work();
        } catch (...) {
        }
        ::_exit(0);
    }
    // the intermediate child exits at once
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
}

} // namespace util
