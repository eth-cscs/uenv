#include <catch2/catch_all.hpp>

// #include <uenv/envvars.h>
#include <util/envvars.h>
#include <util/fs.h>
#include <util/shell.h>

#include <fmt/format.h>
#include <fmt/std.h>

TEST_CASE("which", "[shell]") {
    auto env = envvars::state{environ};

    auto path = env.get("PATH");

    if (!path) {
        SKIP("PATH is not set");
    }

    for (std::string app : {"ls", "which", "find"}) {
        // require that PATH points to some posix tools
        REQUIRE(util::which(app, *path));
    }

    // test that the current executable, named unit, is found
    if (auto exe = util::exe_path()) {
        auto cmd = *exe;
        auto cwd = cmd.parent_path();
        REQUIRE(cmd == *util::which("unit", cwd));
    }
}
