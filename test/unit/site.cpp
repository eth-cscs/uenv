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

    fs::remove_all(root);
}
