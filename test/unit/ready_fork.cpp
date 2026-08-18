#include <csignal>
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
    REQUIRE_THAT(ok.error(),
                 matchers::ContainsSubstring("before signaling"));

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
