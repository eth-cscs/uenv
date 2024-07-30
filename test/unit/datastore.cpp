#include <filesystem>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/datastore.h>
#include <uenv/env.h>

namespace fs = std::filesystem;

TEST_CASE("read-only", "[datastore]") {
    fs::path repo_path{"../test/scratch/repo"};
    auto store = uenv::open_datastore(repo_path);

    if (!store) {
        fmt::println("ERROR: {}", store.error());
    }

    {
        auto results = store->query({{}, {}, {}});
        if (!results) {
            fmt::println("ERROR: {}", results.error());
        }
        REQUIRE(results);

        for (auto& r : *results) {
            fmt::println("{}", r);
        }
    }

    fmt::println("");
    {
        auto results = store->query({"mch", {}, {}});
        if (!results) {
            fmt::println("ERROR: {}", results.error());
        }
        REQUIRE(results);

        for (auto& r : *results) {
            fmt::println("{}", r);
        }
    }

    fmt::println("");
    {
        auto results = store->query({{}, "v7", {}});
        if (!results) {
            fmt::println("ERROR: {}", results.error());
        }
        REQUIRE(results);

        for (auto& r : *results) {
            fmt::println("{}", r);
        }
    }

    fmt::println("");
    {
        auto results = store->query({{}, "24.7", "v1-rc1", {}, "a100"});
        if (!results) {
            fmt::println("ERROR: {}", results.error());
        }
        REQUIRE(results);

        for (auto& r : *results) {
            fmt::println("{}", r);
        }
    }

    fmt::println("");
    {
        auto results = store->query({"wombat", {}, {}});
        if (!results) {
            fmt::println("ERROR: {}", results.error());
        }
        REQUIRE(results);
        REQUIRE(results->empty());

        for (auto& r : *results) {
            fmt::println("{}", r);
        }
    }
}
