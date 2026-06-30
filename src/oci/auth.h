#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <util/expected.h>

namespace oci {

// HTTP basic-auth credentials used to obtain a push/private-pull token.
// (defined here rather than reusing oras::credentials so the oci client does
// not depend on the oras module it is meant to replace.)
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

// --- network operations -------------------------------------------------

// Probe `<registry_url>/v2/` and parse the Bearer challenge from the 401.
util::expected<bearer_challenge, std::string>
discover_challenge(const std::string& registry_url);

// Request a bearer token for the given scopes, optionally authenticating with
// basic-auth credentials (required for push or private pull; omit for anonymous
// pull). Returns the raw token string for use as `Authorization: Bearer <tok>`.
util::expected<std::string, std::string>
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
