#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <functional>

#include <util/detach.h>

namespace util {

namespace {

// point stdin, stdout and stderr at /dev/null and close every other descriptor
void detach_descriptors() {
    int null = ::open("/dev/null", O_RDWR);
    if (null >= 0) {
        ::dup2(null, STDIN_FILENO);
        ::dup2(null, STDOUT_FILENO);
        ::dup2(null, STDERR_FILENO);
    } else {
        ::close(STDIN_FILENO);
        ::close(STDOUT_FILENO);
        ::close(STDERR_FILENO);
    }
    // close_range(2) is not wrapped by glibc before 2.34, and needs Linux 5.9
#ifdef SYS_close_range
    if (::syscall(SYS_close_range, 3u, ~0u, 0u) == 0) {
        return;
    }
#endif
    for (long fd = 3, max = ::sysconf(_SC_OPEN_MAX); fd < max; ++fd) {
        ::close(static_cast<int>(fd));
    }
}

} // namespace

void spawn_detached(const std::function<void()>& work,
                    std::chrono::seconds limit) {
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
        detach_descriptors();
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
