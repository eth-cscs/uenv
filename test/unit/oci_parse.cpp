#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include <oci/parse.h>

using namespace oci;

TEST_CASE("oci parse_digest valid", "[oci][parse]") {
    const std::string hex(64, 'a');
    auto d = parse_digest("sha256:" + hex);
    REQUIRE(d.has_value());
    REQUIRE(d->algorithm() == "sha256");
    REQUIRE(d->hex() == hex);
    REQUIRE(d->string() == "sha256:" + hex);

    // sha512 length is accepted
    REQUIRE(parse_digest("sha512:" + std::string(128, 'f')).has_value());
    // surrounding whitespace is stripped
    REQUIRE(parse_digest("  sha256:" + hex + "  ").has_value());
    // a value made of only digits is valid hex
    REQUIRE(parse_digest("sha256:" + std::string(64, '0')).has_value());
}

TEST_CASE("oci parse_digest rejects malformed", "[oci][parse]") {
    REQUIRE_FALSE(parse_digest(std::string(64, 'a')).has_value()); // no ':'
    REQUIRE_FALSE(parse_digest("md5:" + std::string(32, 'a')).has_value());
    REQUIRE_FALSE(parse_digest("sha256:abcd").has_value()); // too short
    REQUIRE_FALSE(
        parse_digest("sha256:" + std::string(64, 'A')).has_value()); // upper
    REQUIRE_FALSE(
        parse_digest("sha256:" + std::string(63, 'a') + "g").has_value());
    REQUIRE_FALSE(parse_digest("sha256:").has_value()); // no value
    // trailing junk after a complete digest
    REQUIRE_FALSE(
        parse_digest("sha256:" + std::string(64, 'a') + ":x").has_value());
}

TEST_CASE("oci parse_digest error message has a caret", "[oci][parse]") {
    auto d = parse_digest("md5:" + std::string(32, 'a'));
    REQUIRE_FALSE(d.has_value());
    // message() renders the input plus a caret underline.
    const auto msg = d.error().message();
    REQUIRE(msg.find("md5") != std::string::npos);
    REQUIRE(msg.find('^') != std::string::npos);
}

TEST_CASE("oci parse_reference", "[oci][parse]") {
    // a digest string parses to a digest reference
    auto r = parse_reference("sha256:" + std::string(64, 'a'));
    REQUIRE(r.has_value());
    REQUIRE(r->is_digest());

    // a plain tag parses to a tag reference
    auto t = parse_reference("v3");
    REQUIRE(t.has_value());
    REQUIRE(t->is_tag());
    REQUIRE(t->string() == "v3");

    // the referrers-tag schema (sha256-<hex>) is a valid tag, not a digest
    auto rt = parse_reference("sha256-" + std::string(64, 'a'));
    REQUIRE(rt.has_value());
    REQUIRE(rt->is_tag());

    // dotted / underscored tags are valid
    REQUIRE(parse_reference("24.7_v2").has_value());

    // a leading '.' or '-' is rejected, as is empty and an over-long tag
    REQUIRE_FALSE(parse_reference(".bad").has_value());
    REQUIRE_FALSE(parse_reference("-bad").has_value());
    REQUIRE_FALSE(parse_reference("").has_value());
    REQUIRE_FALSE(parse_reference(std::string(129, 'a')).has_value());
}

TEST_CASE("oci parse_bearer_challenge from jfrog", "[oci][parse]") {
    auto c = parse_bearer_challenge(
        "Bearer realm=\"https://jfrog.svc.cscs.ch/v2/token\","
        "service=\"jfrog.svc.cscs.ch\"");
    REQUIRE(c.has_value());
    REQUIRE(c->realm.string() == "https://jfrog.svc.cscs.ch/v2/token");
    REQUIRE(c->service == "jfrog.svc.cscs.ch");
    REQUIRE(c->scopes.empty());
}

TEST_CASE("oci parse_bearer_challenge with a scope containing a comma",
          "[oci][parse]") {
    // the quoted scope value itself contains a comma (pull,push); a naive comma
    // split would break here.
    auto c = parse_bearer_challenge(
        "Bearer realm=\"https://r/token\",service=\"reg\","
        "scope=\"repository:foo/bar:pull,push\"");
    REQUIRE(c.has_value());
    REQUIRE(c->realm.string() == "https://r/token");
    REQUIRE(c->service == "reg");
    REQUIRE(c->scopes ==
            std::vector<std::string>{"repository:foo/bar:pull,push"});
}

TEST_CASE("oci parse_bearer_challenge space-separated scopes", "[oci][parse]") {
    auto c = parse_bearer_challenge(
        "Bearer realm=\"https://r/token\",service=\"reg\","
        "scope=\"repository:a:pull repository:b:pull\"");
    REQUIRE(c.has_value());
    REQUIRE(c->scopes ==
            std::vector<std::string>{"repository:a:pull", "repository:b:pull"});
}

TEST_CASE("oci parse_bearer_challenge rejects non-bearer / no realm",
          "[oci][parse]") {
    REQUIRE_FALSE(parse_bearer_challenge("Basic realm=\"r\"").has_value());
    REQUIRE_FALSE(parse_bearer_challenge("Bearerish realm=\"r\"").has_value());
    REQUIRE_FALSE(parse_bearer_challenge("").has_value());
    REQUIRE_FALSE(parse_bearer_challenge("Bearer service=\"reg\"").has_value());
}

TEST_CASE("oci parse_bearer_challenge is scheme-case-insensitive",
          "[oci][parse]") {
    auto c = parse_bearer_challenge("bearer realm=\"https://r/token\"");
    REQUIRE(c.has_value());
    REQUIRE(c->realm.string() == "https://r/token");
}

TEST_CASE("oci parse_scopes", "[oci][parse]") {
    REQUIRE(parse_scopes("").empty());
    REQUIRE(parse_scopes("   ").empty());
    REQUIRE(parse_scopes("repository:a:pull") ==
            std::vector<std::string>{"repository:a:pull"});
    // leading/trailing/multiple spaces are ignored
    REQUIRE(parse_scopes("  a   b\tc ") ==
            std::vector<std::string>{"a", "b", "c"});
}
