#pragma once

// Internal helpers shared between the oci translation units and their unit
// tests. These are pure functions (no network, no state): URL/path builders
// and response-body parsers. They are deliberately kept out of the public
// client.h/auth.h interfaces — include this header only from src/oci/*.cpp
// and test/unit/oci_*.cpp.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <oci/auth.h>
#include <oci/types.h>
#include <util/url.h>

namespace oci {
namespace detail {

//
// registry URL/path builders
//

std::string blob_path(std::string_view repository, std::string_view digest);

std::string manifest_path(std::string_view repository,
                          std::string_view reference);

std::string uploads_path(std::string_view repository);

std::string tags_path(std::string_view repository);

std::string referrers_path(std::string_view repository,
                           std::string_view digest);

// resolve the Location header of an upload session into the url for the
// monolithic PUT, appending the ?digest= query parameter. The Location may be
// absolute (the registry redirecting the upload to backing storage) or
// registry-relative; an absolute one is untrusted input that is about to
// receive a push token and the blob body, so it must clear check_transport.
util::expected<util::url, std::string>
resolve_upload_url(const util::url& registry_url, std::string_view location,
                   std::string_view digest);

//
// checked JSON field accessors
//

// Registry responses are untrusted input, and nlohmann's value() throws
// json::type_error when a key is present with a mismatched type. These return
// the fallback instead.

// value of the string field `key`, or `fallback` when absent or not a string.
std::string json_string_or(const nlohmann::json& j, const char* key,
                           std::string fallback);

// value of the non-negative integer field `key`, or `fallback` when absent or
// not such a number.
std::size_t json_size_or(const nlohmann::json& j, const char* key,
                         std::size_t fallback);

//
// response-body parsers
//

// parse a /v2/<repo>/tags/list response body.
std::optional<std::vector<std::string>> parse_tags_list(std::string_view body);

// parse the "manifests" array of an OCI image index (a Referrers API
// response, or a referrers tag-schema index) into descriptors.
std::optional<std::vector<descriptor>> parse_referrers(std::string_view body);

//
// token handshake helpers
//

// The bearer-challenge parser lives in src/oci/parse.cpp
// (oci::parse_bearer_challenge).

// build the token endpoint url from a bearer challenge and the requested
// scopes. `service` and the scopes are registry- and caller-supplied, and are
// percent-encoded on the way in.
util::url token_url(const bearer_challenge& challenge,
                    const std::vector<std::string>& scopes);

//
// transport policy
//

// Decide whether `target` — a url the registry handed us in a response header,
// which is about to be fetched *with credentials attached* — is acceptable.
//
// util::curl already refuses to let a redirect downgrade the transport or carry
// credentials to another host (CURLOPT_REDIR_PROTOCOLS_STR, and
// CURLOPT_UNRESTRICTED_AUTH deliberately unset). A Bearer realm and an upload
// Location are fresh requests rather than redirects, so they never reach that
// guard; this applies the same policy to them by hand.
//
// A different *host* is legitimate and common — Docker Hub challenges
// registry-1.docker.io with realm=auth.docker.io, and uploads redirect to blob
// storage — so the host is not constrained. What is constrained is the
// transport:
//   * it must be http or https (never file://, gopher:// and friends: libcurl
//     would happily fetch those), and
//   * it may not be weaker than the transport already in use for `registry`, so
//     an https registry cannot talk us down to cleartext. An http registry (a
//     local test zot) may stay on http.
// `what` names the source for the error message, e.g. "bearer realm".
util::expected<void, std::string> check_transport(const util::url& registry,
                                                  const util::url& target,
                                                  std::string_view what);

// extract the token ("token" or "access_token") and its advertised lifetime
// ("expires_in", when present as an integer) from a token endpoint response
// body.
std::optional<token_response> parse_token_response(std::string_view body);

// build a "repository:<repo>:<actions>" scope string.
std::string repository_scope(std::string_view repository,
                             std::string_view actions);

//
// docker config.json helpers
//

// normalise a docker config.json `auths`/`credHelpers` key (or a registry host)
// down to a bare host[:port] for comparison: drop any scheme, userinfo and
// trailing path, and lowercase. A key that will not parse as a url is returned
// stripped of whitespace only - it cannot match a real host anyway, and a
// malformed entry must not abort the lookup.
std::string normalise_host(std::string_view key);

// the credential helper configured for `host` in a parsed docker config.json.
// A per-registry `credHelpers` entry wins over the global `credsStore`, as in
// docker itself. Returns nullopt when neither names a helper.
std::optional<std::string> helper_for_host(const nlohmann::json& cfg,
                                           std::string_view host);

// parse the stdout of `docker-credential-<helper> get`, which is
// {"ServerURL":..,"Username":..,"Secret":..}. Returns nullopt when the helper
// holds no credential for the registry (it reports empty fields).
util::expected<std::optional<credentials>, std::string>
parse_helper_output(std::string_view body);

} // namespace detail
} // namespace oci
