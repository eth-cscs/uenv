#include <filesystem>
#include <fstream>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <oci/auth.h>
#include <util/fs.h>

// the pure helpers are not part of the public auth.h interface; redeclare their
// prototypes here (they live in namespace oci::impl in auth.cpp).
namespace oci::impl {
std::optional<bearer_challenge> parse_bearer_challenge(std::string_view);
std::string token_url(const bearer_challenge&, const std::vector<std::string>&);
std::optional<std::string> parse_token_response(std::string_view);
std::string repository_scope(std::string_view, std::string_view);
} // namespace oci::impl

using oci::impl::parse_bearer_challenge;
using oci::impl::parse_token_response;
using oci::impl::repository_scope;
using oci::impl::token_url;

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
    oci::bearer_challenge c{
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
    oci::bearer_challenge q{
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

namespace {
// write `content` to `path`, creating/truncating it.
void write_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream f{path};
    f << content;
}
} // namespace

TEST_CASE("oci get_credentials reads a token file", "[oci][auth]") {
    auto dir = util::make_temp_dir();
    auto token_file = dir / "mytoken";
    // only the first line is used as the token
    write_file(token_file, "s3cr3t-token\nsecond line ignored\n");

    auto c = oci::get_credentials(std::optional<std::string>{"alice"},
                                  std::optional<std::string>{token_file.string()});
    REQUIRE(c);              // no error
    REQUIRE(c->has_value()); // credentials resolved
    REQUIRE((*c)->username == "alice");
    REQUIRE((*c)->password == "s3cr3t-token");
}

TEST_CASE("oci get_credentials with no token is anonymous", "[oci][auth]") {
    // no --token -> nullopt credentials (anonymous), never an error, even when a
    // username is supplied.
    auto anon = oci::get_credentials(std::nullopt, std::nullopt);
    REQUIRE(anon);
    REQUIRE_FALSE(anon->has_value());

    auto anon_user =
        oci::get_credentials(std::optional<std::string>{"bob"}, std::nullopt);
    REQUIRE(anon_user);
    REQUIRE_FALSE(anon_user->has_value());
}

TEST_CASE("oci get_credentials errors on a missing token path",
          "[oci][auth]") {
    auto c =
        oci::get_credentials(std::optional<std::string>{"alice"},
                             std::optional<std::string>{"/no/such/path-xyz123"});
    REQUIRE_FALSE(c); // a --token that is not a path/file is an error
}

TEST_CASE("oci get_credentials reads <dir>/TOKEN", "[oci][auth]") {
    auto dir = util::make_temp_dir();
    write_file(dir / "TOKEN", "dir-token\n");

    // passing a directory reads its TOKEN entry
    auto c = oci::get_credentials(std::optional<std::string>{"alice"},
                                  std::optional<std::string>{dir.string()});
    REQUIRE(c);
    REQUIRE(c->has_value());
    REQUIRE((*c)->password == "dir-token");
}

TEST_CASE("oci get_credentials errors when <dir>/TOKEN is absent",
          "[oci][auth]") {
    auto dir = util::make_temp_dir(); // empty directory, no TOKEN
    auto c = oci::get_credentials(std::optional<std::string>{"alice"},
                                  std::optional<std::string>{dir.string()});
    REQUIRE_FALSE(c);
}

TEST_CASE("oci get_credentials falls back to the login name", "[oci][auth]") {
    auto dir = util::make_temp_dir();
    auto token_file = dir / "tok";
    write_file(token_file, "tok\n");

    auto c = oci::get_credentials(std::nullopt,
                                  std::optional<std::string>{token_file.string()});
    // getlogin() resolves a username in a normal session, but may be empty in a
    // detached environment (e.g. some CI) -> the function then reports an error.
    // Assert deterministically on both outcomes.
    if (c) {
        REQUIRE(c->has_value());
        REQUIRE_FALSE((*c)->username.empty());
        REQUIRE((*c)->password == "tok");
    } else {
        REQUIRE(c.error().find("username") != std::string::npos);
    }
}

TEST_CASE("oci credentials formatter redacts the password", "[oci][auth]") {
    oci::credentials c{.username = "alice", .password = "secret"};
    auto s = fmt::format("{}", c);
    REQUIRE(s.find("alice") != std::string::npos);
    REQUIRE(s.find("secret") == std::string::npos); // password must not leak
    REQUIRE(s.find("XXXXXX") != std::string::npos);  // 6 chars -> 6 'X'
}
