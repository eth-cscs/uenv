// vim: ts=4 sts=4 sw=4 et

#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <uenv/env.h>
#include <uenv/settings.h>
#include <uenv/pull.h>
#include <util/envvars.h>

// resolve_and_ensure_uenvs with auto_pull=false must behave identically
// to resolve_uenv_args: same error, same result. This is the contract
// that lets the CLI pass auto_pull=false and get unchanged behavior.
TEST_CASE("resolve_and_ensure_uenvs without auto_pull matches "
          "resolve_uenv_args",
          "[pull]") {
    const uenv::repo_list repos;
    const std::optional<std::string> system_name = std::nullopt;
    const envvars::state env{};
    const std::string desc = "wombat/1.0:v1";

    auto expected = uenv::resolve_uenv_args(desc, repos, system_name);
    auto actual =
        uenv::resolve_and_ensure_uenvs(desc, {}, env, false);

    REQUIRE(!expected);
    REQUIRE(!actual);
    REQUIRE(expected.error() == actual.error());
}

// When the registry is not configured, auto-pull cannot proceed. The
// function must return the original resolution error (the same one
// resolve_uenv_args would produce), not a pull-specific message.
TEST_CASE("resolve_and_ensure_uenvs with auto_pull but no registry "
          "returns original error",
          "[pull]") {
    const uenv::repo_list repos;
    const envvars::state env{};
    const std::string desc = "wombat/1.0:v1";

    auto expected = uenv::resolve_uenv_args(desc, repos, std::nullopt);
    auto actual =
        uenv::resolve_and_ensure_uenvs(desc, {}, env, true);

    REQUIRE(!expected);
    REQUIRE(!actual);
    REQUIRE(expected.error() == actual.error());
}

// File-path descriptions must never be auto-pulled, even when
// auto_pull is true and a registry is configured. A nonexistent file
// should produce the same error as resolve_uenv_args.
TEST_CASE("resolve_and_ensure_uenvs with file path never auto-pulls",
          "[pull]") {
    const uenv::repo_list repos;
    const envvars::state env{};
    const std::string desc = "/nonexistent/squashfs/file.squashfs";

    auto expected = uenv::resolve_uenv_args(desc, repos, std::nullopt);
    auto actual =
        uenv::resolve_and_ensure_uenvs(desc, {}, env, true);

    REQUIRE(!expected);
    REQUIRE(!actual);
    REQUIRE(expected.error() == actual.error());
}
