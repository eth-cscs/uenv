#include <string>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <site/site.h>

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
