#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/env.h>
#include <uenv/lex.h>
#include <uenv/parse.h>

// forward declare private parsers
namespace uenv {
util::expected<std::string, parse_error> parse_name(lexer&);
util::expected<std::string, parse_error> parse_path(lexer&);
util::expected<uenv_description, parse_error> parse_uenv_label(lexer&);
util::expected<uenv_description, parse_error> parse_uenv_description(lexer&);
util::expected<view_description, parse_error> parse_view_description(lexer& L);
} // namespace uenv

TEST_CASE("sanitise inputs", "[parse]") {
    REQUIRE(uenv::sanitise_input("wombat") == "wombat");
    REQUIRE(uenv::sanitise_input("wombat soup") == "wombat soup");
    REQUIRE(uenv::sanitise_input("wombat-soup") == "wombat-soup");
    REQUIRE(uenv::sanitise_input("wombat \nsoup") == "wombat soup");
    REQUIRE(uenv::sanitise_input("wombat\t\nsoup") == "wombatsoup");
    REQUIRE(uenv::sanitise_input("\vwombat soup\r") == "wombat soup");
}

TEST_CASE("parse names", "[parse]") {
    // fmt::println("--- parse name");
    for (const auto& in : {"default", "prgenv-gnu", "a", "x.y", "x_y", "_"}) {
        // fmt::println("{}", in);
        auto L = uenv::lexer(in);
        auto result = uenv::parse_name(L);
        REQUIRE(result);
        REQUIRE(*result == in);
    }
}

TEST_CASE("parse path", "[parse]") {
    // fmt::println("--- parse path");
    for (const auto& in :
         {"etc", "/etc", "/etc.", "/etc/usr/file.txt", "/etc-car/hole_s/_.",
          ".", "./.ssh/config", ".bashrc", "age.txt"}) {
        // fmt::println("  input='{}': ", in);
        auto L = uenv::lexer(in);
        auto result = uenv::parse_path(L);
        REQUIRE(result);
        REQUIRE(*result == in);
    }
}

TEST_CASE("parse view list", "[parse]") {
    for (const auto& in : {"default", "prgenv-gnu:default,wombat"}) {
        fmt::print("=== '{}'\n", in);
        auto result = uenv::parse_view_args(in);
        if (!result) {
            const auto& e = result.error();
            fmt::print("error {} at {}\n", e.msg, e.loc);
        } else {
            for (auto v : *result) {
                fmt::print("  {}\n", v);
            }
        }
    }
}

TEST_CASE("parse uenv list", "[parse]") {
    for (const auto& in :
         {"default", "/foo/bar/store.squashfs,prgenv-gnu/24.7:v1"}) {
        fmt::print("=== '{}'\n", in);
        auto result = uenv::parse_uenv_args(in);
        if (!result) {
            const auto& e = result.error();
            fmt::print("error {} at {}\n", e.msg, e.loc);
        } else {
            for (auto v : *result) {
                fmt::print("  {}\n", v);
            }
        }
    }
}
