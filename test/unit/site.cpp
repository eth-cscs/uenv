#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <site/site.h>
#include <util/fs.h>
#include <util/url.h>

// the listing service reply is parsed from the network: any shape must be an
// error or a filtered set of records, never an exception.
TEST_CASE("parse_registry_listing", "[site]") {
    auto record = [](std::string sha, std::string created, std::string path) {
        return fmt::format(
            R"({{"results": [{{"sha256": "{}", "created": "{}", "path": "{}", "size": 12345}}]}})",
            sha, created, path);
    };
    const std::string sha(64, 'a');
    const std::string good = record(sha, "2024-08-23T16:00:40.123456Z",
                                    "deploy/daint/gh200/prgenv-gnu/24.11/v1");
    {
        auto r = site::parse_registry_listing(good, "deploy");
        REQUIRE(r);
        REQUIRE(r->size() == 1);
        REQUIRE(r->front().name == "prgenv-gnu");
        REQUIRE(r->front().size_byte == 12345);
    }
    {
        // records in another namespace are filtered out
        auto r = site::parse_registry_listing(good, "build");
        REQUIRE(r);
        REQUIRE(r->empty());
    }
    {
        // a record with an invalid date, path or sha is dropped, not fatal
        for (const auto& body : {
                 record("zz", "2024-08-23T16:00:40.123456Z",
                        "deploy/daint/gh200/prgenv-gnu/24.11/v1"),
                 record(sha, "zz", "deploy/daint/gh200/prgenv-gnu/24.11/v1"),
                 record(sha, "2024-08-23T16:00:40.123456Z", "zz"),
             }) {
            INFO(body);
            auto r = site::parse_registry_listing(body, "deploy");
            REQUIRE(r);
            REQUIRE(r->empty());
        }
    }
    {
        // no results, or results that are not the expected type, is not an
        // error
        for (const auto& body :
             {"{}", "null", R"({"results": []})", R"({"results": null})"}) {
            INFO(body);
            auto r = site::parse_registry_listing(body, "deploy");
            REQUIRE(r);
            REQUIRE(r->empty());
        }
    }
    {
        // documents that cannot be interpreted are an error with a message
        for (
            const auto& body : {
                "",
                "not json",
                "[]",
                "42",
                R"({"results": [1]})",
                R"({"results": [{}]})",
                R"({"results": [{"sha256": 1}]})",
                R"({"results": [{"sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "created": "2024-08-23T16:00:40.123456Z", "path": "deploy/daint/gh200/prgenv-gnu/24.11/v1", "size": "big"}]})",
                R"({"results": [{"sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "created": "2024-08-23T16:00:40.123456Z", "path": "deploy/daint/gh200/prgenv-gnu/24.11/v1"}]})",
            }) {
            INFO(body);
            auto r = site::parse_registry_listing(body, "deploy");
            REQUIRE(!r);
            REQUIRE(!r.error().empty());
        }
    }
}

TEST_CASE("registry listing cache", "[site]") {
    namespace fs = std::filesystem;
    const auto root = *util::make_temp_dir();
    const std::string sha(64, 'a');
    const std::string body = fmt::format(
        R"({{"results": [{{"sha256": "{}", "created": "2024-08-23T16:00:40.123456Z", "path": "{}", "size": 1}}]}})",
        sha, "deploy/daint/gh200/prgenv-gnu/24.11/v1");

    SECTION("each listing service has its own directory") {
        const auto other = util::parse_url("http://127.0.0.1:8080/list");
        REQUIRE(site::listing_cache_dir(root, std::nullopt) ==
                site::listing_cache_dir(
                    root, util::parse_url(site::default_listing_url).value()));
        REQUIRE(site::listing_cache_dir(root, std::nullopt) !=
                site::listing_cache_dir(root, other.value()));
        REQUIRE(
            util::is_child(site::listing_cache_dir(root, std::nullopt), root));
    }

    const auto dir = site::listing_cache_dir(root, std::nullopt);

    SECTION("nothing is cached") {
        REQUIRE(!site::cached_registry_listing(dir, "deploy"));
        REQUIRE(site::cached_namespaces(dir).empty());
    }
    SECTION("save and read back") {
        site::save_registry_listing(dir, "deploy", body);
        site::save_registry_listing(dir, "build", R"({"results": []})");
        auto records = site::cached_registry_listing(dir, "deploy");
        REQUIRE(records);
        REQUIRE(records->size() == 1);
        REQUIRE(records->front().name == "prgenv-gnu");
        REQUIRE(site::cached_registry_listing(dir, "build"));
        REQUIRE(site::cached_registry_listing(dir, "build")->empty());
        REQUIRE(!site::cached_registry_listing(dir, "service"));
        REQUIRE(site::cached_namespaces(dir) ==
                std::vector<std::string>{"build", "deploy"});
        // no temporary file is left behind
        REQUIRE(std::distance(fs::directory_iterator(dir),
                              fs::directory_iterator()) == 2);
    }
    SECTION("a listing is replaced") {
        site::save_registry_listing(dir, "deploy", body);
        site::save_registry_listing(dir, "deploy", R"({"results": []})");
        REQUIRE(site::cached_registry_listing(dir, "deploy")->empty());
    }
    SECTION("an old listing is not used") {
        site::save_registry_listing(dir, "deploy", body);
        fs::last_write_time(dir / "deploy.json",
                            fs::file_time_type::clock::now() -
                                site::listing_cache_max_age -
                                std::chrono::hours(1));
        REQUIRE(!site::cached_registry_listing(dir, "deploy"));
    }
    SECTION("an invalid listing is not used") {
        site::save_registry_listing(dir, "deploy", "not json");
        REQUIRE(!site::cached_registry_listing(dir, "deploy"));
    }
    SECTION("a namespace is never a path") {
        for (auto nspace : {"", "..", "../deploy", "a/b", ".hidden"}) {
            INFO(nspace);
            site::save_registry_listing(dir, nspace, body);
            REQUIRE(!site::cached_registry_listing(dir, nspace));
        }
        REQUIRE(!fs::exists(dir.parent_path() / "deploy.json"));
        fs::create_directories(dir);
        std::ofstream(dir / ".hidden.json") << body;
        REQUIRE(site::cached_namespaces(dir).empty());
    }
    SECTION("a listing from the future is not used") {
        site::save_registry_listing(dir, "deploy", body);
        fs::last_write_time(dir / "deploy.json",
                            fs::file_time_type::clock::now() +
                                site::listing_cache_max_age +
                                std::chrono::hours(1));
        REQUIRE(!site::cached_registry_listing(dir, "deploy"));
    }
    SECTION("a listing that is too large is not used") {
        site::save_registry_listing(dir, "deploy", body);
        fs::resize_file(dir / "deploy.json", site::listing_cache_max_size + 1);
        REQUIRE(!site::cached_registry_listing(dir, "deploy"));
    }
    SECTION("a directory in place of the listing is replaced") {
        fs::create_directories(dir / "deploy.json/sub");
        site::save_registry_listing(dir, "deploy", body);
        REQUIRE(site::cached_registry_listing(dir, "deploy"));
    }
    SECTION("only a regular file is read") {
        fs::create_directories(dir);
        REQUIRE(mkfifo((dir / "deploy.json").c_str(), 0600) == 0);
        REQUIRE(!site::cached_registry_listing(dir, "deploy"));
        fs::remove(dir / "deploy.json");
        fs::create_directories(dir / "deploy.json");
        REQUIRE(!site::cached_registry_listing(dir, "deploy"));
    }

    fs::remove_all(root);
}

TEST_CASE("claim_listing_refresh", "[site]") {
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;
    const auto root = *util::make_temp_dir();
    const auto dir = site::listing_cache_dir(root, std::nullopt);
    const auto listing = dir / "deploy.json";
    const auto stamp = dir / ".deploy.refresh";
    const auto now = fs::file_time_type::clock::now();
    const auto interval = site::listing_refresh_interval;
    auto claim = [&](bool usable) {
        return site::claim_listing_refresh(dir, "deploy", usable);
    };
    auto save = [&](fs::file_time_type::duration age) {
        site::save_registry_listing(dir, "deploy", R"({"results": []})");
        fs::last_write_time(listing, now - age);
    };

    SECTION("nothing is cached: the directory is created") {
        REQUIRE(claim(false));
        REQUIRE(fs::exists(stamp));
        // the refresh is claimed once per interval
        REQUIRE(!claim(false));
    }
    SECTION("a fresh listing") {
        save(1s);
        REQUIRE(!claim(true));
        REQUIRE(!fs::exists(stamp));
        // a fresh listing that is unusable is refreshed
        REQUIRE(claim(false));
    }
    SECTION("a stale listing") {
        save(interval + 1s);
        REQUIRE(claim(true));
        REQUIRE(!claim(true));
    }
    SECTION("a listing from the future") {
        save(-(interval + 1s));
        REQUIRE(claim(true));
        // within the interval: the clocks of a network file system can differ
        save(-1s);
        fs::remove(stamp);
        REQUIRE(!claim(true));
    }
    SECTION("an old stamp") {
        save(interval + 1s);
        REQUIRE(claim(true));
        fs::last_write_time(stamp, now - interval - 1s);
        REQUIRE(claim(true));
    }
    SECTION("a stamp from the future") {
        save(interval + 1s);
        REQUIRE(claim(true));
        fs::last_write_time(stamp, now + interval + 1s);
        REQUIRE(claim(true));
    }
    SECTION("the stamp can't be written") {
        save(interval + 1s);
        fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
        if (::access(dir.c_str(), W_OK) == 0) {
            // root can write anyway
            fs::permissions(dir, fs::perms::owner_all);
            SKIP("the directory is writable by root");
        }
        REQUIRE(!claim(true));
        REQUIRE(!claim(false));
        fs::permissions(dir, fs::perms::owner_all);
    }
    SECTION("something other than a file in place of the stamp") {
        save(interval + 1s);
        // it is replaced once it is as old as a stamp would be
        fs::create_directories(stamp / "sub");
        REQUIRE(!claim(true));
        fs::last_write_time(stamp, now - interval - 1s);
        REQUIRE(claim(true));
        REQUIRE(fs::is_regular_file(stamp));
        fs::remove(stamp);
        REQUIRE(mkfifo(stamp.c_str(), 0600) == 0);
        fs::last_write_time(stamp, now - interval - 1s);
        REQUIRE(claim(true));
        REQUIRE(fs::is_regular_file(stamp));
        fs::remove(stamp);
        fs::create_symlink(root / "elsewhere", stamp);
        REQUIRE(claim(true));
        REQUIRE(fs::is_regular_file(fs::symlink_status(stamp)));
        REQUIRE(!fs::exists(root / "elsewhere"));
    }
    SECTION("an invalid namespace") {
        REQUIRE(!site::claim_listing_refresh(dir, "../x", false));
    }

    fs::remove_all(root);
}
