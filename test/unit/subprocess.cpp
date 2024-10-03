#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <util/subprocess.h>

namespace matchers = Catch::Matchers;

TEST_CASE("error", "[subprocess]") {
    // test that execve error is handled correctly by running an binary that
    // does not exist
    {
        auto proc = util::run({"/wombat/soup", "--garbage"});
        REQUIRE(proc->wait() == 1);
        auto line = proc->err.getline();
        REQUIRE(line);
        REQUIRE_THAT(*line,
                     matchers::ContainsSubstring("No such file or directory"));
    }
    // check that errors are handled correctly when an application launches,
    // then fails with an error
    {
        auto proc = util::run({"ls", "--garbage"});
        // ls returns 2 for "serious trouble, e.g. can't access comand line
        // argument"
        REQUIRE(proc->wait() == 2);
        auto line = proc->err.getline();
        REQUIRE(line);
        REQUIRE_THAT(*line,
                     matchers::Equals("ls: unrecognized option '--garbage'"));
    }
}

TEST_CASE("wait", "[subprocess]") {
    Catch::Timer t;
    // sleep for 100 ms
    t.start();
    // wait 50 ms
    auto proc = util::run({"sleep", "0.1s"});
    while (t.getElapsedMilliseconds() < 50) {
    }
    REQUIRE(!proc->finished());
    REQUIRE(proc->wait() == 0);
    REQUIRE(proc->finished());
    REQUIRE(t.getElapsedMicroseconds() > 100'000);
}

TEST_CASE("kill", "[subprocess]") {
    // sleep 100 ms
    auto proc = util::run({"sleep", "0.1s"});
    // the following will be called while the process is running
    proc->kill();
    REQUIRE(proc->finished());
    // se set return value to -1 when the process is killed
    REQUIRE(proc->rvalue() == 9);
}

TEST_CASE("stdout", "[subprocess]") {
    {
        auto proc = util::run({"echo", "hello world"});
        REQUIRE(proc);
        REQUIRE(proc->wait() == 0);
        auto line = proc->out.getline();
        REQUIRE(line);
        REQUIRE_THAT(*line, matchers::Equals("hello world"));
        // only one line of ourput is expected
        REQUIRE(!proc->out.getline());
        REQUIRE(!proc->err.getline());
    }
    {
        auto proc = util::run({"echo", "-e", "hello\nworld"});
        REQUIRE(proc);
        REQUIRE(proc->wait() == 0);
        auto line = proc->out.getline();
        REQUIRE(line);
        REQUIRE_THAT(*line, matchers::Equals("hello"));
        line = proc->out.getline();
        REQUIRE(line);
        REQUIRE_THAT(*line, matchers::Equals("world"));
        // only one line of ourput is expected
        REQUIRE(!proc->out.getline());
        REQUIRE(!proc->err.getline());
    }
}
