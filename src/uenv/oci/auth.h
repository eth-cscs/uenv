#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <util/expected.h>

namespace uenv {
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

// --- pure helpers (no network; unit-tested) -----------------------------

// Parse the value of a `WWW-Authenticate` header. Returns nullopt if it is not
// a Bearer challenge or has no realm. Handles quoted values that themselves
// contain commas (e.g. scope="pull,push") and space-separated scope lists.
std::optional<bearer_challenge>
parse_bearer_challenge(std::string_view header_value);

// Build the token-request URL: <realm>?service=<service>&scope=<s1>&scope=<s2>.
// The scopes we intend to use are passed explicitly (the challenge's own scope
// is only advisory). OCI repository names and pull/push actions contain only
// query-safe characters, so values are placed verbatim.
std::string token_url(const bearer_challenge& challenge,
                      const std::vector<std::string>& scopes);

// Extract the bearer token from a token-endpoint JSON body, accepting both the
// `{"token":...}` and `{"access_token":...}` spellings. nullopt if neither is
// present or the body is not valid JSON.
std::optional<std::string> parse_token_response(std::string_view body);

// Compose an OCI auth scope string: repository:<repository>:<actions>
// e.g. repository_scope("deploy/todi/gh200/app/1.0", "pull,push").
std::string repository_scope(std::string_view repository,
                             std::string_view actions);

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

} // namespace oci
} // namespace uenv
