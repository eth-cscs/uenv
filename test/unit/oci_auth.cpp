#include <catch2/catch_all.hpp>

#include <uenv/oci/auth.h>

using uenv::oci::parse_bearer_challenge;
using uenv::oci::parse_token_response;
using uenv::oci::repository_scope;
using uenv::oci::token_url;

TEST_CASE("oci parse_bearer_challenge from jfrog", "[oci][auth]") {
    // the exact challenge the CSCS jfrog registry returns (spike step 1)
    auto c = parse_bearer_challenge(
        "Bearer realm=\"https://jfrog.svc.cscs.ch/v2/token\","
        "service=\"jfrog.svc.cscs.ch\"");
    REQUIRE(c.has_value());
    REQUIRE(c->realm == "https://jfrog.svc.cscs.ch/v2/token");
    REQUIRE(c->service == "jfrog.svc.cscs.ch");
    REQUIRE(c->scopes.empty());
}

TEST_CASE("oci parse_bearer_challenge with scope containing a comma",
          "[oci][auth]") {
    // the scope value itself contains a comma (pull,push) inside quotes - a
    // naive comma split would break here.
    auto c = parse_bearer_challenge(
        "Bearer realm=\"https://r/token\",service=\"reg\","
        "scope=\"repository:foo/bar:pull,push\"");
    REQUIRE(c.has_value());
    REQUIRE(c->realm == "https://r/token");
    REQUIRE(c->service == "reg");
    REQUIRE(c->scopes == std::vector<std::string>{"repository:foo/bar:pull,push"});
}

TEST_CASE("oci parse_bearer_challenge space-separated scopes", "[oci][auth]") {
    auto c = parse_bearer_challenge(
        "Bearer realm=\"https://r/token\",service=\"reg\","
        "scope=\"repository:a:pull repository:b:pull\"");
    REQUIRE(c.has_value());
    REQUIRE(c->scopes == std::vector<std::string>{"repository:a:pull",
                                                  "repository:b:pull"});
}

TEST_CASE("oci parse_bearer_challenge rejects non-bearer / no realm",
          "[oci][auth]") {
    REQUIRE_FALSE(parse_bearer_challenge("Basic realm=\"r\"").has_value());
    REQUIRE_FALSE(parse_bearer_challenge("Bearerish realm=\"r\"").has_value());
    REQUIRE_FALSE(parse_bearer_challenge("").has_value());
    // a Bearer challenge without a realm is unusable
    REQUIRE_FALSE(
        parse_bearer_challenge("Bearer service=\"reg\"").has_value());
}

TEST_CASE("oci parse_bearer_challenge is scheme-case-insensitive",
          "[oci][auth]") {
    auto c = parse_bearer_challenge("bearer realm=\"https://r/token\"");
    REQUIRE(c.has_value());
    REQUIRE(c->realm == "https://r/token");
}

TEST_CASE("oci token_url", "[oci][auth]") {
    uenv::oci::bearer_challenge c{
        .realm = "https://jfrog.svc.cscs.ch/v2/token",
        .service = "jfrog.svc.cscs.ch",
        .scopes = {}};

    // matches the spike's proven-working token request URL
    REQUIRE(token_url(c, {"repository:uenv/deploy/x/24.7:pull"}) ==
            "https://jfrog.svc.cscs.ch/v2/token?service=jfrog.svc.cscs.ch"
            "&scope=repository:uenv/deploy/x/24.7:pull");

    // multiple scopes become repeated scope= parameters
    REQUIRE(token_url(c, {"repository:a:pull", "repository:b:push"}) ==
            "https://jfrog.svc.cscs.ch/v2/token?service=jfrog.svc.cscs.ch"
            "&scope=repository:a:pull&scope=repository:b:push");

    // a realm that already carries a query continues with '&'
    uenv::oci::bearer_challenge q{
        .realm = "https://r/token?foo=bar", .service = "reg", .scopes = {}};
    REQUIRE(token_url(q, {}) == "https://r/token?foo=bar&service=reg");
}

TEST_CASE("oci parse_token_response", "[oci][auth]") {
    REQUIRE(parse_token_response(R"({"token":"abc123"})") == "abc123");
    REQUIRE(parse_token_response(R"({"access_token":"xyz"})") == "xyz");
    // token preferred when both present
    REQUIRE(parse_token_response(R"({"token":"t","access_token":"a"})") == "t");
    // missing / malformed
    REQUIRE(parse_token_response(R"({"expires_in":300})") == std::nullopt);
    REQUIRE(parse_token_response("not json") == std::nullopt);
    REQUIRE(parse_token_response("") == std::nullopt);
}

TEST_CASE("oci repository_scope", "[oci][auth]") {
    REQUIRE(repository_scope("deploy/todi/gh200/app/1.0", "pull") ==
            "repository:deploy/todi/gh200/app/1.0:pull");
    REQUIRE(repository_scope("foo", "pull,push") == "repository:foo:pull,push");
}
