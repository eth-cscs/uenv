#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/env.h>
#include <uenv/repository.h>

namespace {

auto msha(char c) {
    return uenv::sha256(std::string(64, c));
};
auto mid(char c) {
    return uenv::uenv_id(std::string(16, c));
};

// clang-format off
std::vector<uenv::uenv_record> prgenvgnu_records = {
    {"santis", "gh200", "prgenv-gnu", "23.11", "default", {}, 1024, msha('a'), mid('a')},
    {"santis", "gh200", "prgenv-gnu", "23.11", "v2",      {}, 1024, msha('a'), mid('a')},
    {"santis", "gh200", "prgenv-gnu", "23.11", "v1",      {}, 1024, msha('c'), mid('c')},
    {"santis", "gh200", "prgenv-gnu", "24.2",  "default", {}, 1024, msha('b'), mid('b')},
    {"santis", "gh200", "prgenv-gnu", "24.2",  "v1",      {}, 1024, msha('b'), mid('b')},
};

std::vector<uenv::uenv_record> icon_records = {
    {"santis", "gh200", "icon", "2024", "default", {}, 1024, msha('1'), mid('1')},
    {"santis", "gh200", "icon", "2024", "v2",      {}, 1024, msha('1'), mid('1')},
    {"santis", "gh200", "icon", "2024", "v1",      {}, 5024, msha('2'), mid('2')},
};

// records with the same name/version:tag, that should be disambiguated by hash, system, uarch
std::vector<uenv::uenv_record> duplicate_records = {
    {"santis",  "gh200", "netcdf-tools", "2024", "v1", {}, 1024, msha('w'), mid('w')},
    {"todi",    "gh200", "netcdf-tools", "2024", "v1", {}, 1024, msha('x'), mid('x')},
    {"balfrin", "a100",  "netcdf-tools", "2024", "v1", {}, 1024, msha('y'), mid('y')},
    {"balfrin", "zen3",  "netcdf-tools", "2024", "v1", {}, 1024, msha('z'), mid('z')},
};
// clang-format on

auto create_full_repo() {
    auto repo = uenv::create_repository();
    for (auto& record_set :
         {prgenvgnu_records, icon_records, duplicate_records}) {
        for (auto r : record_set) {
            repo->add(r);
        }
    }

    return repo;
}

} // namespace

TEST_CASE("create-in-memory", "[repository]") {
    auto repo = uenv::create_repository();
    REQUIRE(repo);

    for (auto& r : prgenvgnu_records) {
        REQUIRE(repo->add(r));
    }
}

TEST_CASE("find", "[repository]") {
    auto repo = create_full_repo();
    REQUIRE(repo);

    REQUIRE(repo->query({.name = "prgenv-gnu"})->size() == 5u);
    REQUIRE(repo->query({.name = "prgenv-gnu", .tag = "default"})->size() ==
            2u);
    REQUIRE(repo->query({.name = "prgenv-gnu", .tag = "v1"})->size() == 2u);
    REQUIRE(repo->query({.name = "prgenv-gnu", .version = "23.11"})->size() ==
            3u);
    REQUIRE(repo->query({.name = "prgenv-gnu", .version = "24.2"})->size() ==
            2u);
    REQUIRE(repo->query({.uarch = "gh200"})->size() == 10u);
    REQUIRE(repo->query({.system = "santis"})->size() == 9u);
    REQUIRE(repo->query({.version = "2024", .system = "santis"})->size() == 4u);
}

TEST_CASE("duplicates", "[repository]") {
    // there are 4 records that match "netcdf-tools/2024:v1" that are
    // disambiguated by system and uarch
    // - santis, gh200, sha=wwwww...
    // - todi,   gh200, sha=xxxxx...
    // - balfrin, a100, sha=yyyyy...
    // - balfrin, zen3, sha=zzzzz...
    // different vClusters are expected in the DB.

    auto repo = create_full_repo();
    REQUIRE(repo);

    REQUIRE(repo->query({.name = "netcdf-tools"})->size() == 4u);
    REQUIRE(repo->query({.name = "netcdf-tools", .system = "santis"})->size() ==
            1u);
    REQUIRE(repo->query({.name = "netcdf-tools", .system = "todi"})->size() ==
            1u);
    REQUIRE(
        repo->query({.name = "netcdf-tools", .system = "balfrin"})->size() ==
        2u);
    REQUIRE(
        repo->query(
                {.name = "netcdf-tools", .system = "balfrin", .uarch = "a100"})
            ->size() == 1u);
}

TEST_CASE("search_sha", "[repository]") {
    auto repo = create_full_repo();
    REQUIRE(repo);

    {
        auto sha = msha('z');
        auto result = *(repo->query({.name = sha.string()}));
        REQUIRE(result.size() == 1u);
        REQUIRE(result.begin()->name == "netcdf-tools");
    }

    {
        auto sha = mid('z');
        auto result = *(repo->query({.name = sha.string()}));
        REQUIRE(result.size() == 1u);
        REQUIRE(result.begin()->name == "netcdf-tools");
    }

    {
        auto sha = msha('a');
        auto result = *(repo->query({.name = sha.string()}));
        REQUIRE(result.size() == 2u);
        REQUIRE(result.begin()->name == "prgenv-gnu");
        REQUIRE((result.begin() + 1)->name == "prgenv-gnu");
    }
}
