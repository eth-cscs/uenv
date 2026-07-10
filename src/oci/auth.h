#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <util/expected.h>

namespace oci {

// HTTP basic-auth credentials used to obtain a push/private-pull token.
struct credentials {
    std::string username;
    std::string password; // password or personal access token
};

// A parsed `WWW-Authenticate: Bearer ...` challenge. `realm` is the token
// endpoint; `service` and `scopes` are the parameters to echo back when
// requesting a token.
struct bearer_challenge {
    std::string realm;
    std::string service;
    std::vector<std::string> scopes;

    friend bool operator==(const bearer_challenge&,
                           const bearer_challenge&) = default;
};

// A token issued by the registry's token endpoint: the raw bearer token (for
// `Authorization: Bearer <token>`) plus its advertised lifetime in seconds,
// when the endpoint reported one (`expires_in` is optional in the token
// response; registries that omit it are treated as never-expiring, and an
// expired token is instead recovered by the client's 401 retry).
struct token_response {
    std::string token;
    std::optional<long> expires_in;
};

// --- network operations -------------------------------------------------

// Probe `<registry_url>/v2/` and parse the auth challenge. Returns the parsed
// Bearer challenge on a 401, or `std::nullopt` when the registry permits
// anonymous access (200 on /v2/, no challenge) — in which case the client
// operates tokenless.
util::expected<std::optional<bearer_challenge>, std::string>
discover_challenge(const std::string& registry_url);

// Request a bearer token for the given scopes, optionally authenticating with
// basic-auth credentials (required for push or private pull; omit for anonymous
// pull).
util::expected<token_response, std::string>
fetch_token(const bearer_challenge& challenge,
            const std::vector<std::string>& scopes,
            const std::optional<credentials>& creds = std::nullopt);

// Convenience: discover the challenge for a registry and fetch a token in one
// call.
util::expected<std::string, std::string>
authenticate(const std::string& registry_url,
             const std::vector<std::string>& scopes,
             const std::optional<credentials>& creds = std::nullopt);

// --- credential resolution ----------------------------------------------

// Resolve registry credentials from CLI arguments. `token` is a path to a file
// holding the token string (its first line is read); if `token` names a
// directory, its `TOKEN` entry is used. The username is taken from `username`
// when set, otherwise from the OS login name. Returns `std::nullopt` when no
// `token` is given (anonymous access), or an error string describing why the
// token could not be read.
util::expected<std::optional<credentials>, std::string>
get_credentials(std::optional<std::string> username,
                std::optional<std::string> token);

// The candidate sources consulted by `resolve_credentials`, in precedence
// order. The CLI populates these (it owns the uenv-specific and
// environment-specific paths); this keeps `src/oci` free of any dependency on
// `src/uenv`.
struct credential_sources {
    // an explicit --token path (a file, or a directory holding a `TOKEN` file).
    std::optional<std::filesystem::path> explicit_token;
    // an explicit --username; when unset the OS login name is used.
    std::optional<std::string> username;
    // the uenv token store directory, e.g. $XDG_CONFIG_HOME/uenv/tokens. The
    // token for a registry is read from <dir>/<registry-host>.
    std::optional<std::filesystem::path> uenv_token_dir;
    // a docker config.json path (e.g. ~/.docker/config.json) for interop.
    std::optional<std::filesystem::path> docker_config;
};

// Resolve credentials for `registry_host` from `src`, trying, in order:
//   1. an explicit --token (delegates to get_credentials),
//   2. the uenv token store <uenv_token_dir>/<registry_host>,
//   3. the docker config.json `auths[<registry_host>].auth` entry.
// Returns std::nullopt when no credentials are found (anonymous access), or an
// error string when a source was present but could not be used.
util::expected<std::optional<credentials>, std::string>
resolve_credentials(std::string_view registry_host,
                    const credential_sources& src);

} // namespace oci

#include <fmt/core.h>
// Formats credentials with the password redacted, for safe logging.
template <> class fmt::formatter<oci::credentials> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    template <typename FmtContext>
    constexpr auto format(oci::credentials const& c, FmtContext& ctx) const {
        // replace password characters with 'X'
        return fmt::format_to(ctx.out(), "{{username: {}, password: {:X>{}}}}",
                              c.username, "", c.password.size());
    }
};
