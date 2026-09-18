#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

#include <uenv/complete.h>
#include <uenv/meta.h>
#include <uenv/uenv.h>
#include <util/envvars.h>
#include <util/fs.h>

namespace {

using strings = std::vector<std::string>;

strings values(const std::vector<uenv::candidate>& c) {
    strings v;
    for (auto& x : c) {
        v.push_back(x.value);
    }
    return v;
}

uenv::uenv_record record(std::string name, std::string version, std::string tag,
                         std::string system, std::string uarch) {
    uenv::uenv_record r{};
    r.name = std::move(name);
    r.version = std::move(version);
    r.tag = std::move(tag);
    r.system = std::move(system);
    r.uarch = std::move(uarch);
    return r;
}

const std::vector<uenv::uenv_record> records{
    record("prgenv-gnu", "24.11", "v1", "daint", "gh200"),
    record("prgenv-gnu", "24.11", "v2", "daint", "gh200"),
    record("prgenv-gnu", "24.11", "v1", "eiger", "zen2"),
    record("prgenv-nvfortran", "24.11", "v1", "daint", "gh200"),
    record("netcdf", "4.9", "v1", "eiger", "zen2"),
};

// a directory tree for path completion, with the working directory set to it
// for the lifetime of the fixture
struct tree {
    std::filesystem::path root;
    std::filesystem::path cwd;

    tree() {
        namespace fs = std::filesystem;
        root = *util::make_temp_dir();
        fs::create_directories(root / "images/old");
        fs::create_directories(root / ".hidden");
        for (auto f : {"images/a.squashfs", "images/b.squashfs",
                       "images/notes.txt", "a.squashfs", ".dot.squashfs"}) {
            std::ofstream(root / f) << "x";
        }
        cwd = fs::current_path();
        fs::current_path(root);
    }
    ~tree() {
        std::filesystem::current_path(cwd);
        std::filesystem::remove_all(root);
    }
};

envvars::state home(const std::filesystem::path& p) {
    envvars::state env;
    env.set("HOME", p.string());
    return env;
}

} // namespace

TEST_CASE("complete_label", "[complete]") {
    using uenv::complete_label;
    const std::optional<std::string> daint = "daint";

    REQUIRE(values(complete_label("", records, daint)) ==
            strings{"prgenv-gnu/24.11:v1", "prgenv-gnu/24.11:v2",
                    "prgenv-nvfortran/24.11:v1"});
    REQUIRE(values(complete_label("prgenv-g", records, daint)) ==
            strings{"prgenv-gnu/24.11:v1", "prgenv-gnu/24.11:v2"});
    REQUIRE(values(complete_label("prgenv-gnu/24.11:v2", records, daint)) ==
            strings{"prgenv-gnu/24.11:v2"});
    REQUIRE(
        complete_label("prgenv-gnu/24.11:v2", records, daint)[0].description ==
        "@daint%gh200");
    REQUIRE(values(complete_label("net", records, daint)).empty());

    SECTION("without a system every record is offered once") {
        REQUIRE(values(complete_label("", records, std::nullopt)) ==
                strings{"netcdf/4.9:v1", "prgenv-gnu/24.11:v1",
                        "prgenv-gnu/24.11:v2", "prgenv-nvfortran/24.11:v1"});
    }
    SECTION("@system: every system") {
        REQUIRE(
            values(complete_label("prgenv-gnu/24.11:v1@", records, daint)) ==
            strings{"prgenv-gnu/24.11:v1@daint", "prgenv-gnu/24.11:v1@eiger"});
        REQUIRE(values(complete_label("net@", records, daint)).empty());
        REQUIRE(values(complete_label("netcdf/4.9:v1@e", records, daint)) ==
                strings{"netcdf/4.9:v1@eiger"});
    }
    SECTION("%uarch") {
        REQUIRE(
            values(complete_label("prgenv-gnu/24.11:v1%", records, daint)) ==
            strings{"prgenv-gnu/24.11:v1%gh200"});
        REQUIRE(values(complete_label("prgenv-gnu/24.11:v1@eiger%", records,
                                      daint)) ==
                strings{"prgenv-gnu/24.11:v1@eiger%zen2"});
        REQUIRE(values(complete_label("prgenv-gnu/24.11:v1%zen2@", records,
                                      daint)) ==
                strings{"prgenv-gnu/24.11:v1%zen2@eiger"});
    }
    SECTION("text that would be special in SQL is only text") {
        REQUIRE(values(complete_label("prgenv_", records, daint)).empty());
        REQUIRE(values(complete_label("%", records, daint)).empty());
        REQUIRE(values(complete_label("'", records, daint)).empty());
    }
}

TEST_CASE("complete_uenv_list", "[complete]") {
    using uenv::complete_uenv_list;
    const std::optional<std::string> daint = "daint";
    tree t;
    envvars::state env = home(t.root);

    REQUIRE(values(complete_uenv_list("prgenv-n", records, daint, env)) ==
            strings{"prgenv-nvfortran/24.11:v1"});
    SECTION("comma separated") {
        REQUIRE(values(complete_uenv_list("prgenv-gnu/24.11:v1,prgenv-n",
                                          records, daint, env)) ==
                strings{"prgenv-gnu/24.11:v1,prgenv-nvfortran/24.11:v1"});
        REQUIRE(values(complete_uenv_list("prgenv-gnu,./im", records, daint,
                                          env)) ==
                strings{"prgenv-gnu,./images/"});
    }
    SECTION("squashfs files") {
        REQUIRE(values(complete_uenv_list("./", records, daint, env)) ==
                strings{"./a.squashfs", "./images/"});
        REQUIRE(values(complete_uenv_list("./images/", records, daint, env)) ==
                strings{"./images/a.squashfs", "./images/b.squashfs",
                        "./images/old/"});
        REQUIRE(values(complete_uenv_list("./.", records, daint, env)) ==
                strings{"./../", "./.dot.squashfs", "./.hidden/"});
        REQUIRE(values(complete_uenv_list("~/images/b", records, daint, env)) ==
                strings{"~/images/b.squashfs"});
        auto abs = (t.root / "images/a").string();
        REQUIRE(values(complete_uenv_list(abs, records, daint, env)) ==
                strings{abs + ".squashfs"});
    }
    SECTION("files when there are no labels") {
        REQUIRE(values(complete_uenv_list("", {}, daint, env)) ==
                strings{"./a.squashfs", "./images/"});
        REQUIRE(values(complete_uenv_list("x", {}, daint, env)).empty());
    }
    SECTION("mount points") {
        REQUIRE(values(complete_uenv_list("prgenv-gnu/24.11:v1:./im", records,
                                          daint, env)) ==
                strings{"prgenv-gnu/24.11:v1:./images/"});
        REQUIRE(values(complete_uenv_list("prgenv-gnu:./images/", records,
                                          daint, env)) ==
                strings{"prgenv-gnu:./images/old/"});
        REQUIRE(values(complete_uenv_list("./a.squashfs:./i", records, daint,
                                          env)) ==
                strings{"./a.squashfs:./images/"});
        REQUIRE(values(complete_uenv_list("x,./a.squashfs:~/", records, daint,
                                          env)) ==
                strings{"x,./a.squashfs:~/images/"});
    }
}

TEST_CASE("complete_path", "[complete]") {
    using uenv::complete_path;
    using uenv::path_filter;
    tree t;
    envvars::state env = home(t.root);

    REQUIRE(values(complete_path("", {}, env)) ==
            strings{"a.squashfs", "images/"});
    REQUIRE(values(complete_path("images/", {}, env)) ==
            strings{"images/a.squashfs", "images/b.squashfs",
                    "images/notes.txt", "images/old/"});
    REQUIRE(values(complete_path("images/", {.files = false}, env)) ==
            strings{"images/old/"});
    REQUIRE(values(complete_path("images/", {.glob = "*.txt"}, env)) ==
            strings{"images/notes.txt", "images/old/"});
    REQUIRE(values(complete_path("~", {}, env)) == strings{"~/"});
    REQUIRE(values(complete_path("~/i", {}, env)) == strings{"~/images/"});
    REQUIRE(values(complete_path("~/i", {}, envvars::state{})).empty());
    REQUIRE(values(complete_path("missing/", {}, env)).empty());
    REQUIRE(values(complete_path("images/..", {}, env)) ==
            strings{"images/../"});
    REQUIRE(values(complete_path(".", {.files = false}, env)) ==
            strings{"../", ".hidden/"});
}

TEST_CASE("complete_views", "[complete]") {
    using uenv::complete_views;
    auto meta = [](std::vector<std::string> views) {
        uenv::meta m;
        for (auto& v : views) {
            m.views[v] = {v, v + " view", {}};
        }
        return m;
    };
    std::vector<std::pair<std::string, uenv::meta>> one{
        {"prgenv-gnu", meta({"spack", "default", "modules"})}};
    std::vector<std::pair<std::string, uenv::meta>> two{
        {"prgenv-gnu", meta({"spack", "default"})},
        {"netcdf", meta({"default"})}};

    REQUIRE(values(complete_views("", one)) ==
            strings{"default", "modules", "spack"});
    REQUIRE(complete_views("s", one)[0].description == "spack view");
    REQUIRE(values(complete_views("spack,m", one)) == strings{"spack,modules"});
    REQUIRE(values(complete_views("prgenv-gnu:s", one)) ==
            strings{"prgenv-gnu:spack"});
    REQUIRE(values(complete_views("", two)) == strings{"netcdf:default",
                                                       "prgenv-gnu:default",
                                                       "prgenv-gnu:spack"});
    REQUIRE(values(complete_views("", {})).empty());
}

TEST_CASE("complete_system", "[complete]") {
    REQUIRE(values(uenv::complete_system("", records)) ==
            strings{"daint", "eiger"});
    REQUIRE(values(uenv::complete_system("e", records)) == strings{"eiger"});
}

TEST_CASE("complete_repo", "[complete]") {
    tree t;
    envvars::state env = home(t.root);
    uenv::repo_list repos;
    repos.accumulate(std::vector<uenv::repo_description>{
        {.name = "default", .path = "/repos/default"},
        {.name = "deploy", .path = "/repos/deploy"},
        {.name = "user", .path = "/repos/user"}});

    REQUIRE(values(uenv::complete_repo("de", repos, env)) ==
            strings{"default", "deploy"});
    REQUIRE(uenv::complete_repo("u", repos, env)[0].description ==
            "/repos/user");
    REQUIRE(values(uenv::complete_repo("./", repos, env)) ==
            strings{"./images/"});
    REQUIRE(values(uenv::complete_repo("mine=./i", repos, env)) ==
            strings{"mine=./images/"});
    REQUIRE(values(uenv::complete_repo("user,d", repos, env)) ==
            strings{"user,default", "user,deploy"});
}

TEST_CASE("shell_unquote", "[complete]") {
    using uenv::shell_unquote;
    REQUIRE(shell_unquote("") == "");
    REQUIRE(shell_unquote("abc") == "abc");
    REQUIRE(shell_unquote("'a b'") == "a b");
    REQUIRE(shell_unquote("\"a b\"") == "a b");
    REQUIRE(shell_unquote("a\\ b") == "a b");
    REQUIRE(shell_unquote("'a\\b'") == "a\\b");
    REQUIRE(shell_unquote("\"a\\\"b\\c\"") == "a\"b\\c");
    REQUIRE(shell_unquote("x'y'\"z\"") == "xyz");
    // incomplete words, as they are typed
    REQUIRE(shell_unquote("'a b") == "a b");
    REQUIRE(shell_unquote("\"a") == "a");
    REQUIRE(shell_unquote("a\\") == "a");
}
