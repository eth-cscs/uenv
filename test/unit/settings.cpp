#include <filesystem>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <uenv/settings.h>
#include <util/fs.h>

// namespace fs = std::filesystem;
namespace uenv::impl {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path, const envvars::state&);
}

TEST_CASE("read config files", "[settings]") {
    auto exe = util::exe_path();
    if (!exe) {
        SKIP("unable to find path of unit executable");
    }
    auto config_root = exe->parent_path() / "data/config-files";

    {
        auto result = uenv::impl::read_config_file(config_root / "empty", {});
        REQUIRE(result);
        REQUIRE(!result->repo);
        REQUIRE(!result->color);
    }
    {
        auto result = uenv::impl::read_config_file(config_root / "all", {});
        REQUIRE(result);
        REQUIRE(result->repo);
        REQUIRE(result->repo.value() == "/path/to/config");
        REQUIRE(result->color);
        REQUIRE(result->color.value() == false);
    }
    {
        auto result =
            uenv::impl::read_config_file(config_root / "set-repo", {});
        REQUIRE(result);
        REQUIRE(result->repo);
        REQUIRE(result->repo.value() == "/path/to/config");
        REQUIRE(!result->color);
    }
    {
        envvars::state env{};
        env.set("HOME", "/users/wombat");
        auto result =
            uenv::impl::read_config_file(config_root / "set-repo-envvar", env);
        REQUIRE(result);
        REQUIRE(result->repo);
        REQUIRE(result->repo.value() == "/users/wombat/.uenv");
        REQUIRE(!result->color);
    }

    {
        auto result =
            uenv::impl::read_config_file(config_root / "set-color-true", {});
        REQUIRE(result);
        REQUIRE(!result->repo);
        REQUIRE(result->color);
        REQUIRE(result->color.value() == true);
    }
    {
        auto result =
            uenv::impl::read_config_file(config_root / "set-color-false", {});
        REQUIRE(result);
        REQUIRE(!result->repo);
        REQUIRE(result->color);
        REQUIRE(result->color.value() == false);
    }

    for (auto fname : {"invalid-key", "invalid-line1", "invalid-line2"}) {
        auto result = uenv::impl::read_config_file(config_root / fname, {});
        REQUIRE(!result);
    }
}
