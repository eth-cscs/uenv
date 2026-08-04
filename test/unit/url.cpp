#include <optional>
#include <string>

#include <catch2/catch_all.hpp>

#include <util/url.h>

using namespace util;

namespace {
// parse and unwrap: every input here is expected to be a valid url.
url U(std::string_view text) {
    auto u = parse_url(text);
    REQUIRE(u.has_value());
    return *u;
}
} // namespace

TEST_CASE("parse_url components", "[url]") {
    // scheme-less host with a path, as registry configs are written
    auto a = parse_url("jfrog.svc.cscs.ch/uenv");
    REQUIRE(a.has_value());
    REQUIRE(a->scheme() == url_scheme::none);
    REQUIRE(a->scheme_text() == "");
    REQUIRE(a->host() == "jfrog.svc.cscs.ch");
    REQUIRE(a->path() == "/uenv");
    REQUIRE_FALSE(a->port().has_value());

    // scheme is captured, path preserved
    auto b = parse_url("https://host.io/a/b/");
    REQUIRE(b.has_value());
    REQUIRE(b->scheme() == url_scheme::https);
    REQUIRE(b->host() == "host.io");
    REQUIRE(b->path() == "/a/b/");

    // bare host, no path
    auto c = parse_url("registry.example");
    REQUIRE(c.has_value());
    REQUIRE(c->host() == "registry.example");
    REQUIRE(c->path() == "");

    // port, query and fragment
    auto d = parse_url("http://h:8080/p?a=1&b=2#frag");
    REQUIRE(d.has_value());
    REQUIRE(d->scheme() == url_scheme::http);
    REQUIRE(d->host() == "h");
    REQUIRE(d->port() == 8080u);
    REQUIRE(d->path() == "/p");
    REQUIRE(d->query() == "a=1&b=2");
    REQUIRE(d->fragment() == "frag");

    // IPv6 literal host with a port
    auto e = parse_url("https://[::1]:5000/x");
    REQUIRE(e.has_value());
    REQUIRE(e->host() == "[::1]");
    REQUIRE(e->port() == 5000u);
    REQUIRE(e->path() == "/x");

    // userinfo
    auto f = parse_url("https://user:pass@host/p");
    REQUIRE(f.has_value());
    REQUIRE(f->userinfo() == "user:pass");
    REQUIRE(f->host() == "host");

    // round-trips through string()
    REQUIRE(U("https://h:8080/p?q=1#f").string() == "https://h:8080/p?q=1#f");
}

TEST_CASE("parse_url rejects malformed", "[url]") {
    REQUIRE_FALSE(parse_url("").has_value());
    REQUIRE_FALSE(parse_url("/uenv").has_value());         // no host
    REQUIRE_FALSE(parse_url("bad host/uenv").has_value()); // whitespace in host
    REQUIRE_FALSE(parse_url("host:notaport/x").has_value());
}

// Scheme and host are case-insensitive per RFC 3986 and are lowercased on
// parse. This is what lets a "Location: HTTPS://..." be recognised as absolute
// rather than being mistaken for a relative path, and what makes two spellings
// of the same host compare equal in the credential lookup.
TEST_CASE("parse_url lowercases scheme and host", "[url]") {
    auto u = U("HTTPS://Registry.Example.COM/Path/Case");
    REQUIRE(u.scheme() == url_scheme::https);
    REQUIRE(u.scheme_text() == "https");
    REQUIRE(u.host() == "registry.example.com");
    // the path is NOT case-folded: repository names and digests live there.
    REQUIRE(u.path() == "/Path/Case");

    REQUIRE(U("HTTP://h").scheme() == url_scheme::http);
}

// Parsing is permissive about the scheme on purpose: a `url` means "well
// formed", and refusing a transport is the caller's decision, made where the
// url is about to be fetched.
TEST_CASE("parse_url accepts unknown schemes as `other`", "[url]") {
    REQUIRE(U("gopher://h/x").scheme() == url_scheme::other);
    REQUIRE(U("file://host/x").scheme() == url_scheme::other);
    REQUIRE(U("registry.example").scheme() == url_scheme::none);
}

// A host is required, so an authority-less "scheme:///path" does not parse at
// all. Worth pinning: it means a url that came from a registry header cannot
// name a local file even before the transport policy gets a say.
//
// A scheme is only recognised when followed by "://", so "mailto:a@b.c" is
// indistinguishable from "userinfo@host" and parses as the latter. That is
// harmless here: it yields no scheme, and every caller that fetches a url
// requires http or https.
TEST_CASE("parse_url requires an authority", "[url]") {
    REQUIRE_FALSE(parse_url("file:///etc/passwd").has_value());

    auto m = U("mailto:a@b.c");
    REQUIRE(m.scheme() == url_scheme::none);
    REQUIRE(m.host() == "b.c");
}

// Percent-encoding octets are passed through verbatim, never decoded or
// re-encoded: a digest or repository name in a path must survive byte-exact.
TEST_CASE("parse_url passes percent-encoding through verbatim", "[url]") {
    auto u = U("https://h/a%2Fb%20c");
    REQUIRE(u.path() == "/a%2Fb%20c");
    REQUIRE(u.string() == "https://h/a%2Fb%20c");
}

TEST_CASE("url::host_port", "[url]") {
    REQUIRE(U("https://index.docker.io/v1/").host_port() == "index.docker.io");
    REQUIRE(U("http://127.0.0.1:5000").host_port() == "127.0.0.1:5000");
    REQUIRE(U("host.io/some/path").host_port() == "host.io");
    // userinfo is not part of the host: docker config keys carry it, and a
    // lookup by host must still match.
    REQUIRE(U("https://user@host/v1/").host_port() == "host");
    REQUIRE(U("https://[::1]:5000/x").host_port() == "[::1]:5000");
}

// origin() is the canonicalisation that used to be a trailing-slash strip
// repeated by hand at every use site.
TEST_CASE("url::origin canonicalises the base", "[url]") {
    REQUIRE(U("https://host.io/a/b/").origin().string() == "https://host.io");
    REQUIRE(U("https://host.io/").origin().string() == "https://host.io");
    REQUIRE(U("https://host.io").origin().string() == "https://host.io");
    REQUIRE(U("http://127.0.0.1:5000/uenv").origin().string() ==
            "http://127.0.0.1:5000");
    // userinfo, query and fragment are not part of an origin
    REQUIRE(U("https://user:pass@host/p?q=1#f").origin().string() ==
            "https://host");
}

TEST_CASE("url::resolve joins with exactly one slash", "[url]") {
    const auto base = U("https://host.io").origin();
    REQUIRE(base.resolve("/v2/").string() == "https://host.io/v2/");
    REQUIRE(base.resolve("v2/").string() == "https://host.io/v2/");

    // a base that already ends in '/' must not double it
    REQUIRE(U("https://host.io/").resolve("/v2/").string() ==
            "https://host.io/v2/");
    REQUIRE(U("https://host.io/a/").resolve("/b").string() ==
            "https://host.io/a/b");
    REQUIRE(U("https://host.io/a").resolve("b").string() ==
            "https://host.io/a/b");

    // a trailing slash on the appended path is significant: the OCI base
    // endpoint really is "/v2/".
    REQUIRE(base.resolve("/v2/").path() == "/v2/");
    REQUIRE(base.resolve("").string() == "https://host.io");
}

// The encoder keeps exactly the alphabet of every value uenv sends today, so
// adopting it changes no bytes on the wire; see url::query_param.
TEST_CASE("url::query_param leaves current values byte-identical", "[url]") {
    const auto base = U("https://auth.example/token");

    // an auth scope: ':' '/' and ',' must survive unescaped
    REQUIRE(base.query_param("scope",
                             "repository:uenv/deploy/todi/gh200/app/1.0:pull,"
                             "push")
                .string() ==
            "https://auth.example/token?scope=repository:uenv/deploy/todi/"
            "gh200/app/1.0:pull,push");

    // a digest
    REQUIRE(base.query_param("digest", "sha256:abc123").string() ==
            "https://auth.example/token?digest=sha256:abc123");

    // a repository path
    REQUIRE(base.query_param("from", "uenv/deploy/a-b.c").string() ==
            "https://auth.example/token?from=uenv/deploy/a-b.c");
}

// A registry supplies `service` and `scope` in its WWW-Authenticate header, and
// those values reach a query. Without encoding, a value carrying '&' could add
// parameters of its own.
TEST_CASE("url::query_param escapes query metacharacters", "[url]") {
    const auto base = U("https://auth.example/token");

    REQUIRE(
        base.query_param("service", "x&scope=repository:other:push").query() ==
        "service=x%26scope%3Drepository:other:push");
    REQUIRE(base.query_param("k", "a b").query() == "k=a%20b");
    REQUIRE(base.query_param("k", "a#b").query() == "k=a%23b");
    REQUIRE(base.query_param("k", "a+b").query() == "k=a%2Bb");
    REQUIRE(base.query_param("k", "a%b").query() == "k=a%25b");
    // the key is encoded too
    REQUIRE(base.query_param("a&b", "v").query() == "a%26b=v");
}

// query_param picks the '?'/'&' separator itself, which used to be open-coded
// at each call site.
TEST_CASE("url::query_param appends to an existing query", "[url]") {
    // a realm that already carries a query
    auto u = U("https://auth.example/token?foo=bar");
    REQUIRE(u.query_param("service", "reg").string() ==
            "https://auth.example/token?foo=bar&service=reg");

    // several params chain
    REQUIRE(
        U("https://h/t").query_param("a", "1").query_param("b", "2").string() ==
        "https://h/t?a=1&b=2");

    // a fragment stays after the query rather than swallowing it
    REQUIRE(U("https://h/t#frag").query_param("a", "1").string() ==
            "https://h/t?a=1#frag");
}
