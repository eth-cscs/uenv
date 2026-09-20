#include <csignal>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <catch2/catch_all.hpp>

#include <util/ready_fork.h>

namespace matchers = Catch::Matchers;

// Every child branch below terminates with _exit() rather than
// return/throw, exactly as real callers must: a forked child shares the
// parent's Catch2 runner state, so unwinding normally back into it would
// re-enter and re-run the test machinery inside the child process.

TEST_CASE("create", "[ready_fork]") {
    auto rf = util::ready_fork::create();
    REQUIRE(rf);
}

// parent_pid() is what mount_rootless.cpp's PDEATHSIG-race fix relies on: it
// must be the pid of whoever called create(), captured there since create()
// runs in the parent, before fork().
TEST_CASE("parent_pid matches the creating process", "[ready_fork]") {
    const pid_t self = getpid();
    auto rf = util::ready_fork::create();
    REQUIRE(rf);
    REQUIRE(rf->parent_pid() == self);
}

// and from the child's side, in the ordinary case where the parent is still
// alive, it must agree with getppid().
TEST_CASE("parent_pid matches getppid in the child", "[ready_fork]") {
    auto rf = util::ready_fork::create();
    REQUIRE(rf);

    pid_t pid = rf->fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        _exit(getppid() == rf->parent_pid() ? 0 : 1);
    }

    int status;
    waitpid(pid, &status, 0);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}

TEST_CASE("ready", "[ready_fork]") {
    auto rf = util::ready_fork::create();
    REQUIRE(rf);

    pid_t pid = rf->fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        rf->notify_ready();
        _exit(0);
    }

    auto ok = rf->wait_ready();
    REQUIRE(bool(ok));

    int status;
    waitpid(pid, &status, 0);
}

TEST_CASE("child exits without signaling", "[ready_fork]") {
    auto rf = util::ready_fork::create();
    REQUIRE(rf);

    pid_t pid = rf->fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        _exit(1);
    }

    auto ok = rf->wait_ready();
    REQUIRE(!ok);
    REQUIRE_THAT(ok.error(), matchers::ContainsSubstring("before signaling"));

    int status;
    waitpid(pid, &status, 0);
}

TEST_CASE("child killed by signal before signaling", "[ready_fork]") {
    auto rf = util::ready_fork::create();
    REQUIRE(rf);

    pid_t pid = rf->fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        kill(getpid(), SIGKILL);
        _exit(1);
    }

    auto ok = rf->wait_ready();
    REQUIRE(!ok);

    int status;
    waitpid(pid, &status, 0);
}

// wait_ready()'s blocking read() must retry on EINTR rather than treating
// an interrupted syscall as a failed handshake: arm a timer that delivers
// SIGALRM (with no SA_RESTART) while wait_ready() is blocked, before the
// child has had a chance to signal readiness.
TEST_CASE("wait_ready retries after EINTR", "[ready_fork]") {
    auto rf = util::ready_fork::create();
    REQUIRE(rf);

    pid_t pid = rf->fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        usleep(300'000);
        rf->notify_ready();
        _exit(0);
    }

    struct sigaction sa{};
    sa.sa_handler = [](int) {};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART: the blocking read() must see EINTR
    sigaction(SIGALRM, &sa, nullptr);

    struct itimerval timer{};
    timer.it_value.tv_usec = 100'000;
    setitimer(ITIMER_REAL, &timer, nullptr);

    auto ok = rf->wait_ready();
    REQUIRE(bool(ok));

    int status;
    waitpid(pid, &status, 0);
}

TEST_CASE("fork_and_wait_ready", "[ready_fork]") {
    SECTION("returns once the child is ready") {
        auto pid = util::fork_and_wait_ready([](util::ready_fork& rf) {
            rf.notify_ready();
            _exit(7);
        });
        REQUIRE(pid);
        int status;
        REQUIRE(waitpid(*pid, &status, 0) == *pid);
        REQUIRE(WIFEXITED(status));
        REQUIRE(WEXITSTATUS(status) == 7);
    }
    SECTION("a child that exits before it is ready is an error") {
        auto pid =
            util::fork_and_wait_ready([](util::ready_fork&) { _exit(1); });
        REQUIRE(!pid);
    }
    SECTION("a child that returns does not run the caller's code") {
        const pid_t self = getpid();
        auto pid = util::fork_and_wait_ready([](util::ready_fork&) {});
        REQUIRE(getpid() == self);
        REQUIRE(!pid);
    }
}

TEST_CASE("die_with_parent", "[ready_fork]") {
    // the child forks a grandchild that arms the signal and reports ready,
    // then exits: the grandchild must be killed by the signal
    int fds[2];
    REQUIRE(::pipe(fds) == 0);

    pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ::close(fds[0]);
        auto pid = util::fork_and_wait_ready([](util::ready_fork& rf) {
            if (!rf.die_with_parent(SIGKILL)) {
                _exit(1);
            }
            rf.notify_ready();
            while (true) {
                ::pause();
            }
        });
        // the grandchild holds the write end of fds until it is killed
        _exit(pid ? 0 : 1);
    }

    ::close(fds[1]);
    int status;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);

    // EOF once the grandchild is gone. The alarm bounds the wait if it is not.
    ::alarm(10);
    char c;
    REQUIRE(::read(fds[0], &c, 1) == 0);
    ::alarm(0);
    ::close(fds[0]);
}
