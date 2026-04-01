#include <filesystem>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <toml++/toml.hpp>

#include <uenv/settings.h>
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
