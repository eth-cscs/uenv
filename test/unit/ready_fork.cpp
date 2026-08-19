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
