#include <fstream>
#include <string>
#include <utility>
#include <vector>

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

// malformed meta data must be reported (or tolerated) rather than crash: the
// json is not schema checked, and nlohmann's const operator[] on a missing key
// is an assertion failure in debug builds and undefined behaviour otherwise.
TEST_CASE("load_meta malformed input", "[env]") {
    auto dir = util::make_temp_dir().value();
    auto write = [&](const std::string& name, const std::string& body) {
        auto path = dir / name;
        std::ofstream f(path);
        f << body;
        return path;
    };

    // inputs that must be rejected with an error
    for (const auto& [name, body] :
         std::vector<std::pair<std::string, std::string>>{
             {"empty", ""},
             {"truncated", "{\"mount\": \"/user-env"},
             {"array", "[1, 2, 3]"},
             {"scalar", "42"},
             {"null", "null"},
             {"no-mount", "{\"name\": \"app\"}"},
             {"mount-not-string", "{\"mount\": 7}"},
             {"deep", std::string(100000, '[') + std::string(100000, ']')},
         }) {
        INFO(name);
        auto path = write(name, body);
        auto m = uenv::load_meta(path);
        REQUIRE(!m);
        REQUIRE(!m.error().empty());
    }

    // a missing file is an error, not an exception
    REQUIRE(!uenv::load_meta(dir / "does-not-exist"));

    // inputs with a malformed or incomplete views section load, with the
    // offending views or variables ignored.
    {
        // env.values without "scalar" and without "list"
        auto m = uenv::load_meta(write("no-scalar", R"({
            "mount": "/user-environment",
            "views": {"v": {"env": {"values": {"list": {"PATH": [{"op": "prepend", "value": ["/bin"]}]}}}},
                      "w": {"env": {"values": {}}},
                      "x": {"env": {"values": {"scalar": {"A": "b"}}}}}})"));
        REQUIRE(m);
        REQUIRE(m->views.size() == 3);
        REQUIRE(m->views.contains("v"));
        REQUIRE(m->views.contains("w"));
        REQUIRE(m->views.contains("x"));
    }
    {
        // env without "values", env that is not an object, views that are not
        // objects, entries of the wrong type.
        auto m = uenv::load_meta(write("odd-types", R"({
            "mount": "/user-environment",
            "name": 12,
            "description": null,
            "views": {"a": {"env": {}},
                      "b": {"env": "nope"},
                      "c": 3,
                      "d": null,
                      "e": {"env": {"values": {"list": {"P": [1, {"op": "set"}, {"op": "set", "value": "x"},
                                                              {"op": "set", "value": [1]}]},
                                               "scalar": {"S": 1, "T": null, "U": "u"}}}}},
            "default-view": "missing"})"));
        REQUIRE(m);
        REQUIRE(m->name == "unnamed");
        REQUIRE(m->views.size() == 5);
        // an invalid default view is dropped, not an error
        REQUIRE(!m->default_view);
    }
    {
        // "views" that is not an object is ignored
        auto m = uenv::load_meta(
            write("views-array", R"({"mount": "/x", "views": [1, 2]})"));
        REQUIRE(m);
        REQUIRE(m->views.empty());
        REQUIRE(m->mount == "/x");
    }
}
