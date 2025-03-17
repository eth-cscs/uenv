#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <fmt/std.h>

#include <uenv/env.h>
#include <uenv/log.h>
#include <uenv/meta.h>
#include <util/envvars.h>
#include <util/fs.h>

TEST_CASE("load_meta", "[env]") {
    auto exe = util::exe_path();
    if (!exe) {
        SKIP("unable to find path of unit executable");
    }
    auto meta_path = exe->parent_path() / "data/env-files/cp2k-2024.2-v1.json";
    REQUIRE(uenv::load_meta(meta_path));
}

// an important test: end to end testing of view configuration
// specifically test:
//  - load a view from file
//  - apply the view to a set of environment variables
//  - test that the environment variables are set correctly
// test that:
//  - scalar and prefix variables are set, unset, appended and prepended
//  correctly
//  - multiple views are handled correctly
TEST_CASE("view e2e", "[env]") {
    auto exe = util::exe_path();
    if (!exe) {
        SKIP("unable to find path of unit executable");
    }
    auto meta_root = exe->parent_path() / "data/env-files";
    auto meta = uenv::load_meta(meta_root / "app.json");
    REQUIRE(meta);

    // the initial environment variables
    // PATH = /users/wombat/.local/bin
    //  - will be appended and prepended
    // BOOST_ROOT = /users/wombat/.local/boost
    //  - will be unset
    // CUDA_HOME = /opt/nvidia/cuda
    //  - will be changed
    // WOMBAT = soup
    //  - is not set or modified by the environment
    // SCRATCH = /scratch/name
    //  - used for variable expansion
    envvars::state env{};
    env.set("PATH", "/users/wombat/.local/bin");
    env.set("BOOST_ROOT", "/users/wombat/.local/boost");
    env.set("CUDA_HOME", "/opt/nvidia/cuda");
    env.set("WOMBAT", "soup");
    env.set("SCRATCH", "/scratch/name");

    {
        auto E = env;
        const uenv::concrete_view& view = meta->views["app"];
        E.apply_patch(view.environment, envvars::expand_delim::view);

        REQUIRE(E.get("EMPTYSUB").value() == "/work");
        REQUIRE(E.get("SCRATCHPAD").value() == "/scratch/name/work");
        REQUIRE(E.get("SCRATCH").value() == "/scratch/name");
        REQUIRE(E.get("WOMBAT").value() == "soup");
        REQUIRE(E.get("CUDA_HOME").value() == "/user-environment/env/app");
        REQUIRE(E.get("PATH").value() ==
                "/user-environment/app/bin:/users/wombat/.local/bin:/scratch/"
                "name/local:/usr/bin");
        REQUIRE(!E.get("BOOST_ROOT"));
    }
}
