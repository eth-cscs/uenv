#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <oci/auth.h>
#include <oci/util.h>
#include <util/fs.h>
#include <util/url.h>

using oci::detail::parse_token_response;
using oci::detail::repository_scope;
using oci::detail::token_url;

TEST_CASE("oci token_url", "[oci][auth]") {
    oci::bearer_challenge c{
        .realm = *util::parse_url("https://jfrog.svc.cscs.ch/v2/token"),
        .service = "jfrog.svc.cscs.ch",
        .scopes = {}};

    // matches the spike's proven-working token request URL
    REQUIRE(token_url(c, {"repository:uenv/deploy/x/24.7:pull"}).string() ==
            "https://jfrog.svc.cscs.ch/v2/token?service=jfrog.svc.cscs.ch"
            "&scope=repository:uenv/deploy/x/24.7:pull");

    // multiple scopes become repeated scope= parameters
    REQUIRE(token_url(c, {"repository:a:pull", "repository:b:push"}).string() ==
            "https://jfrog.svc.cscs.ch/v2/token?service=jfrog.svc.cscs.ch"
            "&scope=repository:a:pull&scope=repository:b:push");

    // a realm that already carries a query continues with '&'
    oci::bearer_challenge q{.realm =
                                *util::parse_url("https://r/token?foo=bar"),
                            .service = "reg",
                            .scopes = {}};
    REQUIRE(token_url(q, {}).string() == "https://r/token?foo=bar&service=reg");
}

// `service` is whatever the registry put in its WWW-Authenticate header, and it
// is spliced into a query that decides which scopes the token is minted for.
// Unencoded, a value carrying '&' would smuggle in parameters of its own.
TEST_CASE("oci token_url escapes registry-supplied values", "[oci][auth]") {
    oci::bearer_challenge c{.realm = *util::parse_url("https://r/token"),
                            .service = "x&scope=repository:other:push",
                            .scopes = {}};
    REQUIRE(token_url(c, {}).string() ==
            "https://r/token?service=x%26scope%3Drepository:other:push");

    // a scope carrying a space cannot split into two parameters
    oci::bearer_challenge s{.realm = *util::parse_url("https://r/token"),
                            .service = "",
                            .scopes = {}};
    REQUIRE(token_url(s, {"repository:a b:pull"}).string() ==
            "https://r/token?scope=repository:a%20b:pull");
}

// util::curl already refuses to let a *redirect* downgrade the transport or
// carry credentials across hosts. A realm and an upload Location are fresh
// requests, so they never reach that guard: check_transport applies the same
// policy by hand. A different host is fine - Docker Hub challenges
// registry-1.docker.io with realm=auth.docker.io - the transport is not.
TEST_CASE("oci check_transport", "[oci][auth]") {
    const auto https_reg = *util::parse_url("https://jfrog.svc.cscs.ch");
    const auto http_reg = *util::parse_url("http://127.0.0.1:5000");
    auto ok = [](const util::url& reg, const char* target) {
        return oci::detail::check_transport(reg, *util::parse_url(target), "x")
            .has_value();
    };

    // an https registry may not be talked down to cleartext...
    REQUIRE_FALSE(ok(https_reg, "http://auth.example/token"));
    // ...nor off http(s) entirely: libcurl would happily fetch these.
    REQUIRE_FALSE(ok(https_reg, "gopher://auth.example/token"));
    REQUIRE_FALSE(ok(https_reg, "file://host/etc/passwd"));
    // a scheme-less realm is not a transport we can honour
    REQUIRE_FALSE(ok(https_reg, "auth.example/token"));

    // another https host is legitimate and common
    REQUIRE(ok(https_reg, "https://auth.docker.io/token"));
    REQUIRE(ok(https_reg, "https://jfrog.svc.cscs.ch/v2/token"));

    // an http registry (a local test zot) may stay on http, and may still be
    // upgraded
    REQUIRE(ok(http_reg, "http://127.0.0.1:5000/token"));
    REQUIRE(ok(http_reg, "https://auth.example/token"));
    REQUIRE_FALSE(ok(http_reg, "file://host/x"));
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

// Install an executable `docker-credential-<name>` in `dir` and prepend `dir`
// to PATH for the lifetime of the guard, so that the execvp performed by
// creds_from_helper finds it.
class helper_on_path {
    std::string previous_;

  public:
    helper_on_path(const std::filesystem::path& dir, std::string_view name,
                   std::string_view script) {
        const auto exe = dir / fmt::format("docker-credential-{}", name);
        write_file(exe, script);
        std::filesystem::permissions(exe, std::filesystem::perms::owner_all);
        const auto* path = std::getenv("PATH");
        previous_ = path ? path : "";
        ::setenv("PATH", fmt::format("{}:{}", dir.string(), previous_).c_str(),
                 1);
    }
    ~helper_on_path() {
        ::setenv("PATH", previous_.c_str(), 1);
    }
    helper_on_path(const helper_on_path&) = delete;
    helper_on_path& operator=(const helper_on_path&) = delete;
};
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

// src/oci never guesses who the caller is: a token with no username is an
// error, and the caller (the CLI) turns it into advice naming its own flags.
TEST_CASE("oci get_credentials requires a username", "[oci][auth]") {
    auto dir = util::make_temp_dir().value();
    auto token_file = dir / "tok";
    write_file(token_file, "tok\n");

    auto c = oci::get_credentials(
        std::nullopt, std::optional<std::string>{token_file.string()});
    REQUIRE_FALSE(c);
    REQUIRE(c.error() == oci::username_required_error);
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

    // both token sources funnel through the same pairing helper, so they must
    // fail identically when the caller supplied no username.
    SECTION("explicit token with no username") {
        auto dir = util::make_temp_dir().value();
        auto tok = dir / "tok";
        write_file(tok, "explicit-tok\n");
        oci::credential_sources src;
        src.explicit_token = tok;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE_FALSE(c);
        REQUIRE(c.error() == oci::username_required_error);
    }

    SECTION("token store with no username") {
        auto dir = util::make_temp_dir().value();
        write_file(dir / host, "store-tok\n");
        oci::credential_sources src;
        src.uenv_token_dir = dir;
        auto c = oci::resolve_credentials(host, src);
        REQUIRE_FALSE(c);
        REQUIRE(c.error() == oci::username_required_error);
    }

    // docker config.json auths[host].auth is base64(user:pass). Note that
    // `username` is left unset here and in the sections below: the docker
    // config carries its own username, so the requirement above must not leak
    // into this source.
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

    // an entry with no `auth` and no helper configured for it -> anonymous.
    SECTION("docker config entry with no auth and no helper") {
        auto dir = util::make_temp_dir().value();
        auto cfg = dir / "config.json";
        write_file(cfg, R"({"auths":{"reg.example.com":{}}})");
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

TEST_CASE("oci helper_for_host", "[oci][auth]") {
    using oci::detail::helper_for_host;

    // a per-registry credHelpers entry wins over the global credsStore.
    auto both = nlohmann::json::parse(
        R"({"credsStore":"global","credHelpers":{"reg.example.com":"perreg"}})");
    REQUIRE(helper_for_host(both, "reg.example.com") == "perreg");
    // ... and the credsStore still covers every other registry.
    REQUIRE(helper_for_host(both, "other.example.com") == "global");

    // credHelpers keys may carry a scheme and path, as auths keys do.
    auto scheme = nlohmann::json::parse(
        R"({"credHelpers":{"https://reg.example.com/v2/":"perreg"}})");
    REQUIRE(helper_for_host(scheme, "reg.example.com") == "perreg");

    // no helper configured.
    auto none = nlohmann::json::parse(R"({"auths":{"reg.example.com":{}}})");
    REQUIRE_FALSE(helper_for_host(none, "reg.example.com"));

    // an empty credsStore does not name a helper.
    auto empty = nlohmann::json::parse(R"({"credsStore":""})");
    REQUIRE_FALSE(helper_for_host(empty, "reg.example.com"));

    // wrong-typed values must not throw.
    auto typed = nlohmann::json::parse(R"({"credsStore":42,"credHelpers":[]})");
    REQUIRE_FALSE(helper_for_host(typed, "reg.example.com"));
}

TEST_CASE("oci parse_helper_output", "[oci][auth]") {
    using oci::detail::parse_helper_output;

    SECTION("a stored credential") {
        auto c = parse_helper_output(
            R"({"ServerURL":"reg.example.com","Username":"dave","Secret":"s3cret"})");
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "dave");
        REQUIRE((*c)->password == "s3cret");
    }

    // a helper holding nothing for the registry answers with empty fields.
    SECTION("no stored credential is anonymous") {
        auto c = parse_helper_output(
            R"({"ServerURL":"reg.example.com","Username":"","Secret":""})");
        REQUIRE(c);
        REQUIRE_FALSE(c->has_value());
    }

    // "<token>" means Secret is an identity token, which needs the OAuth2
    // refresh_token grant that fetch_token does not implement.
    SECTION("an identity token is a clear error") {
        auto c =
            parse_helper_output(R"({"Username":"<token>","Secret":"refresh"})");
        REQUIRE_FALSE(c);
        REQUIRE_THAT(c.error(), Catch::Matchers::ContainsSubstring("--token"));
    }

    SECTION("a half-filled credential is an error") {
        REQUIRE_FALSE(
            parse_helper_output(R"({"Username":"dave","Secret":""})"));
        REQUIRE_FALSE(parse_helper_output(R"({"Username":"","Secret":"pw"})"));
    }

    SECTION("malformed output is an error, not a crash") {
        REQUIRE_FALSE(parse_helper_output("not json at all"));
        REQUIRE_FALSE(parse_helper_output("[]"));
    }

    // wrong-typed fields must not throw json::type_error.
    SECTION("wrong-typed fields are treated as absent") {
        auto c = parse_helper_output(R"({"Username":42,"Secret":true})");
        REQUIRE(c);
        REQUIRE_FALSE(c->has_value());
    }
}

TEST_CASE("oci resolve_credentials via a docker credential helper",
          "[oci][auth]") {
    const std::string host = "reg.example.com";
    auto dir = util::make_temp_dir().value();
    const auto cfg = dir / "config.json";
    oci::credential_sources src;
    src.docker_config = cfg;

    // `$(cat)` consumes stdin to EOF, so these also prove that the write end of
    // the child's stdin is closed after the server URL is sent.
    SECTION("credsStore supplies the credential") {
        helper_on_path helper(
            dir, "uenvtest",
            "#!/bin/sh\nserver=$(cat)\n"
            R"(printf '{"ServerURL":"%s","Username":"dave","Secret":"s3cret"}' "$server")"
            "\n");
        write_file(
            cfg, R"({"auths":{"reg.example.com":{}},"credsStore":"uenvtest"})");
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "dave");
        REQUIRE((*c)->password == "s3cret");
    }

    // the helper is keyed by the string the user logged in with, so the
    // configured key is passed on stdin rather than the bare host.
    SECTION("the configured registry key is passed to the helper") {
        helper_on_path helper(
            dir, "uenvecho",
            "#!/bin/sh\nserver=$(cat)\n"
            R"(printf '{"Username":"%s","Secret":"x"}' "$server")"
            "\n");
        write_file(
            cfg,
            R"({"auths":{"https://reg.example.com/v2/":{}},"credsStore":"uenvecho"})");
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "https://reg.example.com/v2/");
    }

    SECTION("credHelpers overrides credsStore") {
        helper_on_path global(dir, "uenvglobal",
                              "#!/bin/sh\ncat >/dev/null\n"
                              R"(printf '{"Username":"global","Secret":"x"}')"
                              "\n");
        helper_on_path perreg(dir, "uenvperreg",
                              "#!/bin/sh\ncat >/dev/null\n"
                              R"(printf '{"Username":"perreg","Secret":"x"}')"
                              "\n");
        write_file(
            cfg,
            R"({"credsStore":"uenvglobal","credHelpers":{"reg.example.com":"uenvperreg"}})");
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE(c->has_value());
        REQUIRE((*c)->username == "perreg");
    }

    // "credentials not found" is how a helper reports an unknown registry.
    SECTION("a helper with no stored credential is anonymous") {
        helper_on_path helper(
            dir, "uenvempty",
            "#!/bin/sh\ncat >/dev/null\n"
            "echo 'credentials not found in native keychain' >&2\nexit 1\n");
        write_file(cfg, R"({"credsStore":"uenvempty"})");
        auto c = oci::resolve_credentials(host, src);
        REQUIRE(c);
        REQUIRE_FALSE(c->has_value());
    }

    // any other failure is surfaced, rather than silently degrading.
    SECTION("a failing helper is an error") {
        helper_on_path helper(dir, "uenvbroken",
                              "#!/bin/sh\ncat >/dev/null\n"
                              "echo 'the keychain is locked' >&2\nexit 2\n");
        write_file(cfg, R"({"credsStore":"uenvbroken"})");
        auto c = oci::resolve_credentials(host, src);
        REQUIRE_FALSE(c);
        REQUIRE_THAT(c.error(), Catch::Matchers::ContainsSubstring(
                                    "the keychain is locked"));
    }

    // execvp fails in the forked child, which exits non-zero having read
    // nothing.
    SECTION("a helper missing from PATH is an error") {
        write_file(cfg, R"({"credsStore":"uenv-definitely-not-installed"})");
        auto c = oci::resolve_credentials(host, src);
        REQUIRE_FALSE(c);
        REQUIRE_THAT(c.error(),
                     Catch::Matchers::ContainsSubstring(
                         "docker-credential-uenv-definitely-not-installed"));
    }

    // A helper that exits without reading leaves the parent writing into a pipe
    // with no reader. Writes below the 64KiB pipe buffer complete regardless,
    // so pin the SIGPIPE guard with a registry key large enough to overrun it:
    // without the guard the write kills the process with signal 13.
    SECTION("a helper that never reads stdin does not kill the process") {
        helper_on_path helper(dir, "uenvdeaf", "#!/bin/sh\nexit 0\n");
        const std::string padding(200000, 'x');
        write_file(cfg, fmt::format(R"({{"auths":{{"https://{}/{}":{{}}}},)"
                                    R"("credsStore":"uenvdeaf"}})",
                                    host, padding));
        auto c = oci::resolve_credentials(host, src);
        REQUIRE_FALSE(c); // the helper answers nothing, which is an error
    }
}
