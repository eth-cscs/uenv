#include <cerrno>
#include <cstring>
#include <sys/prctl.h>
#include <unistd.h>

#include <fmt/format.h>

#include <util/ready_fork.h>

namespace util {

ready_fork::ready_fork(int read_fd, int write_fd, pid_t parent_pid)
    : read_fd_(read_fd), write_fd_(write_fd), parent_pid_(parent_pid) {
}

ready_fork::~ready_fork() {
    if (read_fd_ >= 0) {
        (void)::close(read_fd_);
    }
    if (write_fd_ >= 0) {
        (void)::close(write_fd_);
    }
}

expected<ready_fork, std::string> ready_fork::create() {
    int fds[2];
    if (::pipe(fds) != 0) {
        return unexpected(fmt::format("pipe() failed: {}", strerror(errno)));
    }
    return expected<ready_fork, std::string>(std::in_place, fds[0], fds[1],
                                             ::getpid());
}

pid_t ready_fork::parent_pid() const {
    return parent_pid_;
}

expected<void, std::string> ready_fork::die_with_parent(int sig) {
    if (::prctl(PR_SET_PDEATHSIG, sig) != 0) {
        return unexpected(
            fmt::format("prctl(PR_SET_PDEATHSIG) failed: {}", strerror(errno)));
    }
    // the kernel reparents atomically, so this tells definitively whether the
    // parent exited before the signal was armed
    if (::getppid() != parent_pid_) {
        return unexpected(std::string{
            "parent process exited before PDEATHSIG could be armed"});
    }
    return {};
}

pid_t ready_fork::fork() {
    pid_t pid = ::fork();
    if (pid == 0) {
        (void)::close(read_fd_);
        read_fd_ = -1;
    } else if (pid > 0) {
        (void)::close(write_fd_);
        write_fd_ = -1;
    }
    return pid;
}

expected<void, std::string> ready_fork::notify_ready() {
    char buf[32];
    memset(buf, 'x', sizeof(buf));
    // AppImage seems to do something more advanced:
    // https://github.com/AppImage/AppImageKit/blob/master/src/runtime.c#L138
    ssize_t n = ::write(write_fd_, buf, sizeof(buf));
    (void)::close(write_fd_);
    write_fd_ = -1;
    if (n < 0) {
        return unexpected(fmt::format("write() failed: {}", strerror(errno)));
    }
    return {};
}

expected<void, std::string> ready_fork::wait_ready() {
    char buf[256];
    int res;
    do {
        res = ::read(read_fd_, buf, sizeof(buf));
    } while (res < 0 && errno == EINTR);
    (void)::close(read_fd_);
    read_fd_ = -1;
    if (res < 0) {
        return unexpected(fmt::format("read() failed: {}", strerror(errno)));
    }
    if (res == 0) {
        return unexpected(
            std::string{"child exited before signaling readiness"});
    }
    return {};
}

expected<pid_t, std::string>
fork_and_wait_ready(const std::function<void(ready_fork&)>& child) {
    auto rf = ready_fork::create();
    if (!rf) {
        return unexpected(rf.error());
    }

    const pid_t pid = rf->fork();
    if (pid < 0) {
        return unexpected(fmt::format("fork() failed: {}", strerror(errno)));
    }
    if (pid == 0) {
        child(*rf);
        _exit(0);
    }

    if (auto ok = rf->wait_ready(); !ok) {
        return unexpected(ok.error());
    }
    return pid;
}

} // namespace util
