#include <filesystem>
#include <fstream>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <oci/auth.h>
#include <oci/util.h>
#include <util/fs.h>

using oci::detail::parse_token_response;
using oci::detail::repository_scope;
using oci::detail::token_url;

TEST_CASE("oci token_url", "[oci][auth]") {
    oci::bearer_challenge c{.realm = "https://jfrog.svc.cscs.ch/v2/token",
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
    auto t = parse_token_response(R"({"token":"abc123"})");
    REQUIRE(t.has_value());
    REQUIRE(t->token == "abc123");
    REQUIRE(!t->expires_in.has_value());

    auto a = parse_token_response(R"({"access_token":"xyz"})");
    REQUIRE(a.has_value());
    REQUIRE(a->token == "xyz");

    // token preferred when both present
    auto both = parse_token_response(R"({"token":"t","access_token":"a"})");
    REQUIRE(both.has_value());
    REQUIRE(both->token == "t");

    // the advertised lifetime is extracted when it is an integer
    auto exp = parse_token_response(R"({"token":"t","expires_in":300})");
    REQUIRE(exp.has_value());
    REQUIRE(exp->expires_in == 300);

    // a non-integer expires_in is ignored, not an error
    auto bad_exp = parse_token_response(R"({"token":"t","expires_in":"300"})");
    REQUIRE(bad_exp.has_value());
    REQUIRE(!bad_exp->expires_in.has_value());

    // missing / malformed
    REQUIRE(!parse_token_response(R"({"expires_in":300})").has_value());
    REQUIRE(!parse_token_response("not json").has_value());
    REQUIRE(!parse_token_response("").has_value());
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
    auto dir = util::make_temp_dir().value();
    auto token_file = dir / "mytoken";
    // only the first line is used as the token
    write_file(token_file, "s3cr3t-token\nsecond line ignored\n");

    auto c =
        oci::get_credentials(std::optional<std::string>{"alice"},
                             std::optional<std::string>{token_file.string()});
    REQUIRE(c);              // no error
    REQUIRE(c->has_value()); // credentials resolved
    REQUIRE((*c)->username == "alice");
    REQUIRE((*c)->password == "s3cr3t-token");
}

TEST_CASE("oci get_credentials with no token is anonymous", "[oci][auth]") {
    // no --token -> nullopt credentials (anonymous), never an error, even when
    // a username is supplied.
    auto anon = oci::get_credentials(std::nullopt, std::nullopt);
    REQUIRE(anon);
    REQUIRE_FALSE(anon->has_value());

    auto anon_user =
        oci::get_credentials(std::optional<std::string>{"bob"}, std::nullopt);
    REQUIRE(anon_user);
    REQUIRE_FALSE(anon_user->has_value());
}

TEST_CASE("oci get_credentials errors on a missing token path", "[oci][auth]") {
    auto c = oci::get_credentials(
        std::optional<std::string>{"alice"},
        std::optional<std::string>{"/no/such/path-xyz123"});
    REQUIRE_FALSE(c); // a --token that is not a path/file is an error
}

TEST_CASE("oci get_credentials reads <dir>/TOKEN", "[oci][auth]") {
    auto dir = util::make_temp_dir().value();
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
    auto dir = util::make_temp_dir().value(); // empty directory, no TOKEN
    auto c = oci::get_credentials(std::optional<std::string>{"alice"},
                                  std::optional<std::string>{dir.string()});
    REQUIRE_FALSE(c);
}

TEST_CASE("oci get_credentials falls back to the login name", "[oci][auth]") {
    auto dir = util::make_temp_dir().value();
    auto token_file = dir / "tok";
    write_file(token_file, "tok\n");

    auto c = oci::get_credentials(
        std::nullopt, std::optional<std::string>{token_file.string()});
    // getlogin() resolves a username in a normal session, but may be empty in a
    // detached environment (e.g. some CI) -> the function then reports an
    // error. Assert deterministically on both outcomes.
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
    REQUIRE(s.find("XXXXXX") != std::string::npos); // 6 chars -> 6 'X'
}

TEST_CASE("oci resolve_credentials precedence", "[oci][auth]") {
    const std::string host = "reg.example.com";

    // an explicit --token wins over everything else.
    SECTION("explicit token") {
        auto dir = util::make_temp_dir().value();
        auto tok = dir / "tok";
        write_file(tok, "explicit-tok\n");
        oci::credential_sources src;
        src.explicit_token = tok;
        src.username = "alice";
        src.uenv_token_dir = dir; // present but must be ignored
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "alice");
        REQUIRE((*c)->password == "explicit-tok");
    }

    // the uenv token store: <dir>/<host>.
    SECTION("uenv token store") {
        auto dir = util::make_temp_dir().value();
        write_file(dir / host, "store-tok\n");
        oci::credential_sources src;
        src.username = "bob";
        src.uenv_token_dir = dir;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "bob");
        REQUIRE((*c)->password == "store-tok");
    }

    // docker config.json auths[host].auth is base64(user:pass).
    SECTION("docker config fallback") {
        auto dir = util::make_temp_dir().value();
        auto cfg = dir / "config.json";
        // echo -n carol:pw | base64 -> Y2Fyb2w6cHc=
        write_file(cfg,
                   R"({"auths":{"reg.example.com":{"auth":"Y2Fyb2w6cHc="}}})");
        oci::credential_sources src;
        src.docker_config = cfg;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "carol");
        REQUIRE((*c)->password == "pw");
    }

    // docker keys may carry a scheme/path; matching is on the bare host.
    SECTION("docker config host normalisation") {
        auto dir = util::make_temp_dir().value();
        auto cfg = dir / "config.json";
        write_file(
            cfg,
            R"({"auths":{"https://reg.example.com/v2/":{"auth":"Y2Fyb2w6cHc="}}})");
        oci::credential_sources src;
        src.docker_config = cfg;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "carol");
    }

    // no matching source -> anonymous (nullopt), not an error.
    SECTION("no source is anonymous") {
        auto dir = util::make_temp_dir().value(); // empty store, no host file
        oci::credential_sources src;
        src.uenv_token_dir = dir;
        src.docker_config = dir / "does-not-exist.json";
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE_FALSE(c->has_value());
    }

    // a config entry with no host match -> anonymous.
    SECTION("docker config host miss") {
        auto dir = util::make_temp_dir().value();
        auto cfg = dir / "config.json";
        write_file(
            cfg, R"({"auths":{"other.example.com":{"auth":"Y2Fyb2w6cHc="}}})");
        oci::credential_sources src;
        src.docker_config = cfg;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE_FALSE(c->has_value());
    }

    // a credsStore-only entry (no `auth`) is skipped, not an error.
    SECTION("docker config credsStore entry skipped") {
        auto dir = util::make_temp_dir().value();
        auto cfg = dir / "config.json";
        write_file(
            cfg, R"({"auths":{"reg.example.com":{}},"credsStore":"desktop"})");
        oci::credential_sources src;
        src.docker_config = cfg;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE_FALSE(c->has_value());
    }

    // malformed base64 in the auth field is an error.
    SECTION("docker config malformed auth") {
        auto dir = util::make_temp_dir().value();
        auto cfg = dir / "config.json";
        write_file(cfg, R"({"auths":{"reg.example.com":{"auth":"@@@@"}}})");
        oci::credential_sources src;
        src.docker_config = cfg;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE_FALSE(c);
    }
}
