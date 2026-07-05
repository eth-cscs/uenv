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

TEST_CASE("oci parse_url components", "[oci][parse]") {
    // scheme-less host with a path
    auto a = parse_url("jfrog.svc.cscs.ch/uenv");
    REQUIRE(a.has_value());
    REQUIRE(a->scheme == "");
    REQUIRE(a->host == "jfrog.svc.cscs.ch");
    REQUIRE(a->path == "/uenv");
    REQUIRE_FALSE(a->port.has_value());

    // scheme is captured, path preserved
    auto b = parse_url("https://host.io/a/b/");
    REQUIRE(b.has_value());
    REQUIRE(b->scheme == "https");
    REQUIRE(b->host == "host.io");
    REQUIRE(b->path == "/a/b/");

    // bare host, no path
    auto c = parse_url("registry.example");
    REQUIRE(c.has_value());
    REQUIRE(c->host == "registry.example");
    REQUIRE(c->path == "");

    // port, query and fragment
    auto d = parse_url("http://h:8080/p?a=1&b=2#frag");
    REQUIRE(d.has_value());
    REQUIRE(d->scheme == "http");
    REQUIRE(d->host == "h");
    REQUIRE(d->port == 8080u);
    REQUIRE(d->path == "/p");
    REQUIRE(d->query == "a=1&b=2");
    REQUIRE(d->fragment == "frag");

    // IPv6 literal host with a port
    auto e = parse_url("https://[::1]:5000/x");
    REQUIRE(e.has_value());
    REQUIRE(e->host == "[::1]");
    REQUIRE(e->port == 5000u);
    REQUIRE(e->path == "/x");

    // userinfo
    auto f = parse_url("https://user:pass@host/p");
    REQUIRE(f.has_value());
    REQUIRE(f->userinfo == "user:pass");
    REQUIRE(f->host == "host");

    // round-trips through string()
    REQUIRE(parse_url("https://h:8080/p?q=1#f")->string() ==
            "https://h:8080/p?q=1#f");
}

TEST_CASE("oci parse_url rejects malformed", "[oci][parse]") {
    REQUIRE_FALSE(parse_url("").has_value());
    REQUIRE_FALSE(parse_url("/uenv").has_value());         // no host
    REQUIRE_FALSE(parse_url("bad host/uenv").has_value()); // whitespace in host
    REQUIRE_FALSE(parse_url("host:notaport/x").has_value());
}

TEST_CASE("oci parse_bearer_challenge from jfrog", "[oci][parse]") {
    auto c = parse_bearer_challenge(
        "Bearer realm=\"https://jfrog.svc.cscs.ch/v2/token\","
        "service=\"jfrog.svc.cscs.ch\"");
    REQUIRE(c.has_value());
    REQUIRE(c->realm == "https://jfrog.svc.cscs.ch/v2/token");
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
    REQUIRE(c->realm == "https://r/token");
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
    REQUIRE(c->realm == "https://r/token");
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
