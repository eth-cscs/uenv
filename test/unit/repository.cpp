#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/env.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/fs.h>

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

auto create_mini_repo(std::optional<std::filesystem::path> p = {}) {
    auto repo = p ? uenv::create_repository(*p) : uenv::create_repository();
    // clang-format off
    std::vector<uenv::uenv_record> wombat_records = {
        {"arapiles", "gh200", "wombat", "2024", "v2",  {}, 1024, msha('a'), mid('a')},
        {"arapiles", "gh200", "dingo",  "2024", "v2",  {}, 1024, msha('a'), mid('a')},
        {"arapiles", "gh200", "wombat", "2024", "v1",  {}, 5024, msha('b'), mid('b')},
    };
    // clang-format on
    for (auto& r : wombat_records) {
        repo->add(r);
    }

    return repo;
}

} // namespace

TEST_CASE("create-in-memory", "[repository]") {
    auto repo = uenv::create_repository();
    REQUIRE(repo);
    REQUIRE(repo->is_in_memory());

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

TEST_CASE("remove sha", "[repository]") {
    auto repo = create_mini_repo();
    REQUIRE(repo);

    auto num_images = [&repo]() { return repo->query({})->size(); };

    // remove an image that does not exist
    // this is not an error
    REQUIRE(repo->remove(msha('7')));
    REQUIRE(num_images() == 3);

    // delete a sha that corresponds to one image
    {
        auto sha = msha('b');

        // double check that there is 1 image with this sha
        {
            auto result = *(repo->query({.name = sha.string()}));
            REQUIRE(result.size() == 1u);
        }

        // remove the sha
        REQUIRE(repo->remove(sha));

        // check that there are now no matches
        {
            auto result = *(repo->query({.name = sha.string()}));
            REQUIRE(result.size() == 0u);
        }
    }
    // 1 images should have been removed
    REQUIRE(num_images() == 2);

    // delete a sha that corresponds to two images
    {
        auto sha = msha('a');

        // double check that there are 2 images with this sha
        {
            auto result = *(repo->query({.name = sha.string()}));
            REQUIRE(result.size() == 2u);
        }

        // remove the sha
        REQUIRE(repo->remove(sha));

        // check that there are now no matches
        {
            auto result = *(repo->query({.name = sha.string()}));
            REQUIRE(result.size() == 0u);
        }
    }

    // 2 images should have been removed
    REQUIRE(num_images() == 0);
}

TEST_CASE("remove label", "[repository]") {
    auto R = create_mini_repo();
    REQUIRE(R);
    auto& repo = *R;

    auto num_images = [&repo]() { return repo.query({})->size(); };

    auto uenv_a = *(repo.query({.name = msha('a').string()}));
    auto uenv_b = *(repo.query({.name = msha('b').string()}));

    REQUIRE(uenv_a.size() == 2u);
    REQUIRE(uenv_b.size() == 1u);

    REQUIRE(repo.remove(*uenv_b.begin()));
    REQUIRE(num_images() == 2);

    REQUIRE(repo.remove(*uenv_a.begin()));
    REQUIRE(num_images() == 1);

    REQUIRE(repo.remove(*(uenv_a.begin() + 1)));
    REQUIRE(num_images() == 0);
}

TEST_CASE("create disk repo", "[repository]") {
    auto repo_dir = util::make_temp_dir();
    {
        auto R = create_mini_repo(repo_dir);
        REQUIRE(R);
        REQUIRE(!R->is_in_memory());
    }
    {
        auto R = uenv::open_repository(repo_dir);
        REQUIRE(R);
        REQUIRE(!R->is_in_memory());
        REQUIRE(R->query({})->size() == 3);
    }
}

TEST_CASE("pick repo", "[repository]") {
    using namespace std::string_literals;
    namespace fs = std::filesystem;

    std::vector<uenv::repo_description> descriptions = {
        {.name = "user", .path = "/home/name/uenv", .priority = 10},
        {.name = "system", .path = "/site/public/uenv", .priority = 20},
        {.name = "lab",
         .path = "/capstor/store/g123/shared/uenv",
         .priority = 5},
    };

    // match a single entry by name
    {
        auto result =
            uenv::pick_repo(uenv::repo_label{"system"s}, descriptions, "cli");
        REQUIRE(result);
        auto& v = result.value();
        REQUIRE(v.name == "system");
        REQUIRE(v.path == "/site/public/uenv");
        REQUIRE(v.priority == uenv::repo_description::default_priority);
    }

    // choose using only name something not in the list
    // this should fail, because names are used to select from a pre-defined
    // repo
    {
        auto result =
            uenv::pick_repo(uenv::repo_label{"wombat"s}, descriptions, "cli");
        REQUIRE(!result);
    }
    // pick a path not in the list
    {
        auto result = uenv::pick_repo(uenv::repo_label{fs::path{"/usr/.uenv"}},
                                      descriptions, "cli");
        REQUIRE(result);
        REQUIRE(result->name == "cli");
        REQUIRE(result->path == "/usr/.uenv");
        REQUIRE(result->priority == uenv::repo_description::default_priority);
    }
}

TEST_CASE("filter repo list", "[repository]") {
    using namespace std::string_literals;
    namespace fs = std::filesystem;

    {
        std::vector<uenv::repo_description> descriptions = {
            {.name = "user", .path = "/home/name/uenv", .priority = 10},
            {.name = "system", .path = "/site/public/uenv", .priority = 20},
            {.name = "lab",
             .path = "/capstor/store/g123/shared/uenv",
             .priority = 5},
        };
        // an empty filter list should return an empty list
        {
            auto result = uenv::filter_repo_list({}, descriptions, false);
            REQUIRE((result && result->empty()));
        }

        // match a single entry by name
        {
            auto result =
                uenv::filter_repo_list({{"system"s}}, descriptions, false);
            REQUIRE(result);
            auto& v = result.value();
            REQUIRE(v.size() == 1u);
            REQUIRE(v[0].name == "system");
            REQUIRE(v[0].path == "/site/public/uenv");
            // the priority is reset to the default after filtering
            REQUIRE(v[0].priority == uenv::repo_description::default_priority);
        }

        // match a single entry by path
        {
            auto result = uenv::filter_repo_list(
                {{fs::path("/home/uenv-repo")}}, descriptions, false);
            if (!result) {
                fmt::println("BOOM!! {}", result.error());
            }
            REQUIRE(result);
            auto& v = result.value();
            REQUIRE(v.size() == 1u);
            REQUIRE(v[0].name == "anonymous");
            REQUIRE(v[0].path == "/home/uenv-repo");
            REQUIRE(v[0].priority == uenv::repo_description::default_priority);
        }
        // match a description
        {
            auto result = uenv::filter_repo_list(
                {{uenv::repo_name_path{.name = "wombat",
                                       .path = "/home/burrow/uenv"}}},
                descriptions, false);
            if (!result) {
                fmt::println("BOOM!! {}", result.error());
            }
            REQUIRE(result);
            auto& v = result.value();
            REQUIRE(v.size() == 1u);
            REQUIRE(v[0].name == "wombat");
            REQUIRE(v[0].path == "/home/burrow/uenv");
            REQUIRE(v[0].priority == uenv::repo_description::default_priority);
        }
        // match a description, path and named lookup
        {
            auto result = uenv::filter_repo_list(
                {
                    uenv::repo_name_path{.name = "wombat",
                                         .path = "/home/burrow/uenv"},

                    fs::path("/home/uenv-repo"),
                    "system"s,
                },
                descriptions, false);
            if (!result) {
                fmt::println("BOOM!! {}", result.error());
            }
            REQUIRE(result);
            auto& v = result.value();
            REQUIRE(v.size() == 3u);
            REQUIRE(v[0].name == "wombat");
            REQUIRE(v[0].path == "/home/burrow/uenv");
            REQUIRE(v[0].priority == uenv::repo_description::default_priority);
            REQUIRE(v[1].name == "anonymous");
            REQUIRE(v[1].path == "/home/uenv-repo");
            REQUIRE(v[1].priority == uenv::repo_description::default_priority);
            REQUIRE(v[2].name == "system");
            REQUIRE(v[2].path == "/site/public/uenv");
            REQUIRE(v[2].priority == uenv::repo_description::default_priority);
        }
        // inalid name
        {
            auto result =
                uenv::filter_repo_list({{"kangaroo"s}}, descriptions, false);
            REQUIRE(!result);
            REQUIRE(result.error() ==
                    "there is no repo with the name kangaroo");
        }
    }
}

// helper: make a repo_description with default priority
static uenv::repo_description
rd(std::string name, std::string path,
   uint32_t priority = uenv::repo_description::default_priority) {
    return {.name = std::move(name),
            .path = std::filesystem::path(std::move(path)),
            .priority = priority};
}

TEST_CASE("repo_list accumulate priority ordering", "[repo_list]") {
    uenv::repo_list rl;
    // lower priority number sorts first
    rl.accumulate({rd("high", "/high", 5), rd("low", "/low", 20)});
    REQUIRE(rl.size() == 2u);
    REQUIRE(rl[0].name == "high");
    REQUIRE(rl[1].name == "low");
}

TEST_CASE("repo_list accumulate tie-breaking: incoming before existing",
          "[repo_list]") {
    uenv::repo_list rl;
    rl.accumulate({rd("existing", "/existing")});
    rl.accumulate({rd("incoming", "/incoming")});
    // both at default_priority; incoming (more-specific layer) sorts first
    REQUIRE(rl.size() == 2u);
    REQUIRE(rl[0].name == "incoming");
    REQUIRE(rl[1].name == "existing");
}

TEST_CASE("repo_list accumulate conflict: same name, incoming path wins",
          "[repo_list]") {
    uenv::repo_list rl;
    rl.accumulate({rd("myrepo", "/old/path")});
    rl.accumulate({rd("myrepo", "/new/path")});
    REQUIRE(rl.size() == 1u);
    REQUIRE(rl[0].name == "myrepo");
    REQUIRE(rl[0].path == "/new/path");
}

TEST_CASE("repo_list accumulate conflict: same path, incoming name wins",
          "[repo_list]") {
    uenv::repo_list rl;
    rl.accumulate({rd("old-name", "/shared/path")});
    rl.accumulate({rd("new-name", "/shared/path")});
    REQUIRE(rl.size() == 1u);
    REQUIRE(rl[0].name == "new-name");
    REQUIRE(rl[0].path == "/shared/path");
}

TEST_CASE("repo_list accumulate conflict: same name and path deduplicates",
          "[repo_list]") {
    uenv::repo_list rl;
    rl.accumulate({rd("repo", "/path")});
    rl.accumulate({rd("repo", "/path")});
    REQUIRE(rl.size() == 1u);
}

TEST_CASE("repo_list accumulate multiple layers: later wins ties",
          "[repo_list]") {
    // simulate default → system → user accumulation
    uenv::repo_list rl;
    rl.accumulate({rd("default", "/default", 9)});
    rl.accumulate({rd("sys", "/sys")});
    rl.accumulate({rd("usr", "/usr")});
    // default has lower priority number → first; usr and sys both at
    // default_priority, usr was accumulated last so sorts before sys
    REQUIRE(rl.size() == 3u);
    REQUIRE(rl[0].name == "default");
    REQUIRE(rl[1].name == "usr");
    REQUIRE(rl[2].name == "sys");
}

TEST_CASE("repo_list replace: name-only label resolves from current contents",
          "[repo_list]") {
    using namespace std::string_literals;
    uenv::repo_list rl;
    rl.accumulate({rd("team", "/team/repo"), rd("shared", "/shared/repo")});

    auto result = rl.replace({"team"s});
    REQUIRE(result);
    REQUIRE(rl.size() == 1u);
    REQUIRE(rl[0].name == "team");
    REQUIRE(rl[0].path == "/team/repo");
    REQUIRE(rl[0].priority == uenv::repo_description::default_priority);
}

TEST_CASE("repo_list replace: unknown name returns error", "[repo_list]") {
    using namespace std::string_literals;
    uenv::repo_list rl;
    rl.accumulate({rd("team", "/team/repo")});
    auto result = rl.replace({"nosuchrepo"s});
    REQUIRE(!result);
}

TEST_CASE("repo_list replace: path-only label generates name from basename",
          "[repo_list]") {
    // Use a real temp dir with a leading-dot basename to exercise name
    // stripping: ".uenv-images-test" -> "uenv-images-test"
    auto tmpdir = std::filesystem::temp_directory_path() / ".uenv-images-test";
    std::filesystem::create_directories(tmpdir);

    uenv::repo_list rl;
    auto result = rl.replace({tmpdir});
    REQUIRE(result);
    REQUIRE(rl.size() == 1u);
    REQUIRE(rl[0].name == "uenv-images-test");
    REQUIRE(rl[0].path == tmpdir);

    std::filesystem::remove(tmpdir);
}

TEST_CASE("repo_list replace: name+path label used directly", "[repo_list]") {
    auto tmpdir = std::filesystem::temp_directory_path();
    uenv::repo_list rl;
    auto result =
        rl.replace({uenv::repo_name_path{.name = "custom", .path = tmpdir}});
    REQUIRE(result);
    REQUIRE(rl.size() == 1u);
    REQUIRE(rl[0].name == "custom");
    REQUIRE(rl[0].path == tmpdir);
}

TEST_CASE("repo_list replace: preserves input order at default_priority",
          "[repo_list]") {
    using namespace std::string_literals;
    uenv::repo_list rl;
    rl.accumulate({rd("a", "/a"), rd("b", "/b"), rd("c", "/c")});

    auto result = rl.replace({"c"s, "a"s, "b"s});
    REQUIRE(result);
    REQUIRE(rl.size() == 3u);
    REQUIRE(rl[0].name == "c");
    REQUIRE(rl[1].name == "a");
    REQUIRE(rl[2].name == "b");
}
