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
        h,
        "WWW-Authenticate: Bearer realm=\"https://r/token\",service=\"r\"\r\n");
    util::curl::parse_header_line(
        h, "Location: https://r/v2/x/blobs/uploads/abc123\r\n");
    util::curl::parse_header_line(h,
                                  "Docker-Content-Digest: sha256:deadbeef\r\n");

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
    util::curl::parse_header_line(h,
                                  "  Location :  https://host:443/path \r\n");
    REQUIRE(h.get("location") == "https://host:443/path");

    // empty value is preserved
    util::curl::parse_header_line(h, "X-Empty:\r\n");
    REQUIRE(h.get("x-empty") == "");
}

TEST_CASE("curl http_message", "[curl]") {
    auto contains = [](const std::string& s, std::string_view sub) {
        return s.find(sub) != std::string::npos;
    };
    const std::string service_desk = "service desk";

    // the credential-related codes describe every place credentials are
    // looked for, in the order oci::resolve_credentials consults them, plus
    // the flag that supplies the username that sources 1 and 2 lack.
    for (long code : {401L, 403L}) {
        const auto m = util::curl::http_message(code);
        REQUIRE(contains(m, "--token"));
        REQUIRE(contains(m, "~/.config/uenv/tokens/"));
        REQUIRE(contains(m, "~/.docker/config.json"));
        REQUIRE(contains(m, "--username"));
    }

    // ... and do not send the user to the service desk: these are the user's
    // credentials, not a broken service.
    REQUIRE_FALSE(contains(util::curl::http_message(401), service_desk));
    REQUIRE_FALSE(contains(util::curl::http_message(403), service_desk));

    // an unmapped 4xx is reported factually, with the status, and likewise
    // carries no service desk advice
    for (long code : {404L, 405L, 413L, 429L}) {
        const auto m = util::curl::http_message(code);
        REQUIRE(contains(m, std::to_string(code)));
        REQUIRE_FALSE(contains(m, service_desk));
    }

    // 408 keeps its bespoke retry advice
    REQUIRE(contains(util::curl::http_message(408), "retry"));

    // 5xx and anything outside the 4xx range fall through to the generic
    // "contact CSCS" message
    for (long code : {500L, 502L, 503L, 0L}) {
        REQUIRE(contains(util::curl::http_message(code), service_desk));
    }
}
