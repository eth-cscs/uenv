#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <toml++/toml.hpp>

#include <uenv/settings.h>
#include <util/envvars.h>
#include <util/fs.h>

namespace uenv::impl::v1 {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path, const envvars::state&);
} // namespace uenv::impl::v1

namespace uenv::impl::v2 {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path, const envvars::state&);

util::expected<config_base, uenv::config_error>
parse_config_toml(const toml::table& input, const envvars::state& calling_env);
} // namespace uenv::impl::v2

TEST_CASE("read config files v1", "[settings]") {
    auto exe = util::exe_path();
    if (!exe) {
        SKIP("unable to find path of unit executable");
    }
    auto config_root = exe->parent_path() / "data/config-files";

    using namespace uenv::impl::v1;

    {
        auto result = read_config_file(config_root / "empty", {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
        REQUIRE(!result->color);
    }
    {
        auto result = read_config_file(config_root / "all", {});
        REQUIRE(result);
        REQUIRE(!result->repos.empty());
        REQUIRE(result->repos.front().path == "/path/to/config");
        REQUIRE(result->color);
        REQUIRE(result->color.value() == false);
    }
    {
        auto result = read_config_file(config_root / "set-repo", {});
        REQUIRE(result);
        REQUIRE(!result->repos.empty());
        REQUIRE(result->repos.front().path == "/path/to/config");
        REQUIRE(!result->color);
    }
    {
        envvars::state env{};
        env.set("HOME", "/users/wombat");
        auto result = read_config_file(config_root / "set-repo-envvar", env);
        REQUIRE(result);
        REQUIRE(!result->repos.empty());
        REQUIRE(result->repos.front().path == "/users/wombat/.uenv");
        REQUIRE(!result->color);
    }

    {
        auto result = read_config_file(config_root / "set-color-true", {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
        REQUIRE(result->color);
        REQUIRE(result->color.value() == true);
    }
    {
        auto result = read_config_file(config_root / "set-color-false", {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
        REQUIRE(result->color);
        REQUIRE(result->color.value() == false);
    }

    for (auto fname : {"invalid-key", "invalid-line1", "invalid-line2"}) {
        auto result = read_config_file(config_root / fname, {});
        REQUIRE(!result);
    }
}

TEST_CASE("read config toml", "[settings]") {
    // the location of the test inputs is relative to the path of the current
    // executable
    auto exe = util::exe_path();
    if (!exe) {
        SKIP("unable to find path of unit executable");
    }
    auto config_root = exe->parent_path() / "data/config-files";

    using namespace uenv::impl::v2;
    using namespace std::string_view_literals;

    // check reading from a file
    {
        auto result = read_config_file(config_root / "empty.toml", {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
        REQUIRE(!result->color);
        REQUIRE(!result->elastic_config);
    }
    {
        auto result = read_config_file(config_root / "all.toml", {});
        REQUIRE(result);
        REQUIRE(!result->repos.empty());
        const auto& repo = result->repos.front();
        REQUIRE(repo.path == "/home/repo");
        REQUIRE(repo.name == "main");
        REQUIRE(result->color);
        REQUIRE(result->color.value() == true);
        REQUIRE(result->elastic_config);
        REQUIRE(result->elastic_config.value() == "https://url/elastic:3000");
    }

    // check parsing of raw toml input
    {
        const auto input = toml::parse("color = true");
        auto result = uenv::impl::v2::parse_config_toml(input, {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
        REQUIRE(result->color);
        REQUIRE(result->color.value() == true);
        REQUIRE(!result->elastic_config);
    }
    {
        const auto input = toml::parse("color = false");
        auto result = parse_config_toml(input, {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
        REQUIRE(result->color);
        REQUIRE(result->color.value() == false);
        REQUIRE(!result->elastic_config);
    }

    {
        const std::string_view input = R"(
color=true
[elastic]
url = "https://my-elastic")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
        REQUIRE(result->color);
        REQUIRE(result->color.value() == true);
        REQUIRE(result->elastic_config);
        REQUIRE(result->elastic_config.value() == "https://my-elastic");
    }
    {
        // registry config incl. the optional listing_url override
        const std::string_view input = R"(
[registry]
url = "jfrog.svc.cscs.ch/uenv"
default_namespace = "deploy"
listing_url = "http://127.0.0.1:8080/list")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE(result);
        REQUIRE(result->registry);
        // the url is parsed at load, and a scheme-less config value - the form
        // the CSCS deployment uses - is defaulted to https.
        REQUIRE(result->registry->url.string() ==
                "https://jfrog.svc.cscs.ch/uenv");
        REQUIRE(result->registry->default_namespace == "deploy");
        REQUIRE(result->registry->listing_url);
        REQUIRE(result->registry->listing_url->string() ==
                "http://127.0.0.1:8080/list");
    }
    {
        // listing_url is optional
        const std::string_view input = R"(
[registry]
url = "jfrog.svc.cscs.ch/uenv"
default_namespace = "deploy")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE(result);
        REQUIRE(result->registry);
        REQUIRE(!result->registry->listing_url);
    }
    {
        // an explicit http:// is honoured, so a local registry can be used
        const std::string_view input = R"(
[registry]
url = "http://127.0.0.1:5000/uenv"
default_namespace = "deploy")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE(result);
        REQUIRE(result->registry->url.string() == "http://127.0.0.1:5000/uenv");
    }
    {
        // a malformed registry url is caught when the config is read, rather
        // than half way through a push, and the error names the key.
        const std::string_view input = R"(
[registry]
url = "bad host/uenv"
default_namespace = "deploy")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE_FALSE(result);
        REQUIRE(result.error().message.find("registry.url") !=
                std::string::npos);
    }
    {
        // only http(s) can be spoken to a registry; anything else is refused at
        // the boundary rather than by whatever eventually tries to fetch it.
        const std::string_view input = R"(
[registry]
url = "file://somewhere/uenv"
default_namespace = "deploy")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE_FALSE(result);
        REQUIRE(result.error().message.find("http or https") !=
                std::string::npos);
    }
    {
        // the optional endpoints are validated too, and named
        const std::string_view input = R"(
[registry]
url = "jfrog.svc.cscs.ch/uenv"
default_namespace = "deploy"
listing_url = "bad host/list")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE_FALSE(result);
        REQUIRE(result.error().message.find("registry.listing_url") !=
                std::string::npos);
    }
    {
        const std::string_view input = R"(
[[repositories]]
name = "therepo"
path = "/home/bobsmith/.uenvrepo")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE(result);
        REQUIRE(result->repos.size() == 1u);
        REQUIRE(result->repos[0].path == "/home/bobsmith/.uenvrepo");
        REQUIRE(result->repos[0].name == "therepo");
        REQUIRE(!result->color);
        REQUIRE(!result->elastic_config);
    }

    // check variable expansion in repository path
    {
        const std::string_view input = R"(
[[repositories]]
name = "envrepo"
path = "${REPO}/.uenv")"sv;
        auto env = envvars::state{};
        env.set("REPO", "/repopath");
        auto result = parse_config_toml(toml::parse(input), env);
        REQUIRE(result);
        REQUIRE(result->repos.size() == 1u);
        const auto& repo = result->repos.front();
        REQUIRE(repo.path == "/repopath/.uenv");
        REQUIRE(repo.name == "envrepo");
    }

    // check error reporting
    {
        auto result = parse_config_toml(toml::parse("wombat = true"), {});
        REQUIRE(!result);
        REQUIRE(fmt::format("{}", result.error()) ==
                "(line 1) unexpected key 'wombat'");
    }
    {
        const std::string_view input = R"([[repositories]]
path = "/home/bobsmith/.uenvrepo"
home = "hello" # this will be an error")"sv;
        auto result = parse_config_toml(toml::parse(input), {});
        REQUIRE(!result);
        REQUIRE(fmt::format("{}", result.error()) ==
                "(line 3) unexpected key 'home'");
    }
    {
        // parsing of an invalid toml file
        auto result = read_config_file(config_root / "error.toml", {});
        REQUIRE(!result);
    }
}

// malformed documents are reported as a configuration error (or accepted),
// never an assertion failure, a stack overflow or an exception.
TEST_CASE("read config files v2 malformed", "[settings]") {
    using namespace uenv::impl::v2;

    auto dir = util::make_temp_dir().value();
    auto write = [&](const std::string& name, const std::string& body) {
        auto path = dir / name;
        std::ofstream f(path);
        f << body;
        return path;
    };

    {
        // an empty repositories array is valid and adds nothing
        auto result =
            read_config_file(write("empty-repos", "repositories = []\n"), {});
        REQUIRE(result);
        REQUIRE(result->repos.empty());
    }
    {
        // a DEL character where a key is expected
        auto result = read_config_file(write("del", "[\n\x7f"), {});
        REQUIRE(!result);
        REQUIRE(result.error().find("line 1") != std::string::npos);
    }
    {
        // a non-ascii code point in a bare key position
        auto result = read_config_file(write("latin1", "1\xc3\x81\t"), {});
        REQUIRE(!result);
    }
    {
        // a dotted key with more segments than the parser's nesting limit: the
        // nested tables are walked and destroyed recursively, so the depth has
        // to be bounded before it can overflow the stack
        std::string deep = "[";
        for (int i = 0; i < 200000; ++i) {
            deep += "a.";
        }
        deep += "a]\nx = 1\n";
        auto result = read_config_file(write("deep-key", deep), {});
        REQUIRE(!result);
        REQUIRE(result.error().find("dotted key depth") != std::string::npos);

        // the same limit applies to a key-value pair
        std::string deep_kv;
        for (int i = 0; i < 200000; ++i) {
            deep_kv += "a.";
        }
        deep_kv += "a = 1\n";
        REQUIRE(!read_config_file(write("deep-kv", deep_kv), {}));
    }
    {
        // a date-time separator that is not followed by a time, a value that
        // looks like a local date-time written with a space but ends in a
        // comment, and a value terminator where an array element is expected
        for (const auto& body :
             {"url = 1979-05-27Turl07:32:00\n", "a = 1979-05-27T\n",
              "e = 10p7-07-00 5#\n", "a = 1979-05-27 5#\n", "a = [}\n",
              "a = [[[#\n", "a = [1, ]]\n"}) {
            INFO(body);
            REQUIRE(!read_config_file(write("date", body), {}));
        }
    }
    {
        // deeply nested arrays and inline tables are refused by toml++'s
        // nesting limit
        REQUIRE(!read_config_file(
            write("deep-array", "a = " + std::string(100000, '[') +
                                    std::string(100000, ']') + "\n"),
            {}));
    }
    {
        // a repository path the filesystem cannot represent is a config
        // error, not an exception from std::filesystem
        for (const auto& path : {"/" + std::string(5000, 'a'),
                                 "/" + std::string(300, 'a') + "/b"}) {
            auto result = read_config_file(
                write("long-path",
                      "[[repositories]]\nname = 'a'\npath = '" + path + "'\n"),
                {});
            REQUIRE(!result);
            REQUIRE(result.error().find("longer than") != std::string::npos);
        }
    }
    {
        // an unterminated ${ in a path is passed through, not read past the
        // end of the value
        envvars::state env{};
        env.set("HOME", "/users/wombat");
        auto result = read_config_file(
            write("unterminated",
                  "[[repositories]]\nname = 'a'\npath = '${HOME'\n"),
            env);
        REQUIRE(!result);
    }
    {
        // wrong types for every key
        for (const auto& body : {
                 "color = 'yes'\n",
                 "system_name = 1\n",
                 "system_name = ''\n",
                 "repositories = 1\n",
                 "repositories = [1]\n",
                 "[[repositories]]\nname = 1\npath = '/x'\n",
                 "[[repositories]]\nname = 'a'\npath = 1\n",
                 "[[repositories]]\nname = 'a'\n",
                 "[[repositories]]\npath = '/x'\n",
                 "registry = 1\n",
                 "[registry]\nurl = 1\n",
                 "[registry]\nurl = 'a.b'\n",
                 "[registry]\nurl = 'ftp://a.b'\ndefault_namespace = 'd'\n",
                 "[registry]\nurl = 'a.b'\ndefault_namespace = 1\n",
                 "elastic = 'x'\n",
                 "[elastic]\nurl = 1\n",
                 "[elastic]\n",
             }) {
            INFO(body);
            auto result = read_config_file(write("bad", body), {});
            REQUIRE(!result);
            REQUIRE(!result.error().empty());
        }
    }
}

TEST_CASE("user_cache_path", "[settings]") {
    envvars::state env;
    REQUIRE(!uenv::user_cache_path(env));
    env.set("HOME", "/home/user");
    REQUIRE(uenv::user_cache_path(env) == "/home/user/.cache/uenv");
    env.set("XDG_CACHE_HOME", "/cache");
    REQUIRE(uenv::user_cache_path(env) == "/cache/uenv");
}
