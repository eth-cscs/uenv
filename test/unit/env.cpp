#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/env.h>

TEST_CASE("env", "[parse_view]") {
    for (const auto &in : {"default", "prgenv-gnu:default,wombat"}) {
        fmt::print("=== '{}'\n", in);
        auto result = uenv::parse_view_args(in);
        if (!result) {
            const auto &e = result.error();
            fmt::print("error {} at {}\n", e.msg, e.loc);
        } else {
            for (auto v : *result) {
                fmt::print("  {}\n", v);
            }
        }
    }
}
