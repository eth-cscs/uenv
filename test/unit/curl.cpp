#include <catch2/catch_all.hpp>

#include <util/curl.h>

TEST_CASE("curl headers are case-insensitive", "[curl]") {
    util::curl::headers h;
    h.set("Content-Type", "application/json");

    REQUIRE(h.get("Content-Type") == "application/json");
    REQUIRE(h.get("content-type") == "application/json");
    REQUIRE(h.get("CONTENT-TYPE") == "application/json");
    REQUIRE(h.get("missing") == std::nullopt);

    // last value seen for a name wins
    h.set("content-type", "text/plain");
    REQUIRE(h.get("Content-Type") == "text/plain");
}

TEST_CASE("curl parse_header_line", "[curl]") {
    util::curl::headers h;

    // the OCI-relevant headers the registry client needs to capture
    util::curl::parse_header_line(
        h, "WWW-Authenticate: Bearer realm=\"https://r/token\",service=\"r\"\r\n");
    util::curl::parse_header_line(
        h, "Location: https://r/v2/x/blobs/uploads/abc123\r\n");
    util::curl::parse_header_line(
        h, "Docker-Content-Digest: sha256:deadbeef\r\n");

    REQUIRE(h.get("www-authenticate") ==
            "Bearer realm=\"https://r/token\",service=\"r\"");
    REQUIRE(h.get("location") == "https://r/v2/x/blobs/uploads/abc123");
    REQUIRE(h.get("docker-content-digest") == "sha256:deadbeef");
}

TEST_CASE("curl parse_header_line edge cases", "[curl]") {
    util::curl::headers h;

    // status line and blank separator carry no colon -> ignored
    util::curl::parse_header_line(h, "HTTP/1.1 200 OK\r\n");
    util::curl::parse_header_line(h, "\r\n");
    REQUIRE(h.entries.empty());

    // surrounding whitespace is trimmed from name and value; a value may itself
    // contain a colon (e.g. a URL or digest).
    util::curl::parse_header_line(h, "  Location :  https://host:443/path \r\n");
    REQUIRE(h.get("location") == "https://host:443/path");

    // empty value is preserved
    util::curl::parse_header_line(h, "X-Empty:\r\n");
    REQUIRE(h.get("x-empty") == "");
}
