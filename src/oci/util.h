#pragma once

// Internal helpers shared between the oci translation units and their unit
// tests. These are pure functions (no network, no state): URL/path builders
// and response-body parsers. They are deliberately kept out of the public
// client.h/auth.h interfaces — include this header only from src/oci/*.cpp
// and test/unit/oci_*.cpp.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <oci/auth.h>
#include <oci/client.h>

namespace oci {
namespace detail {

// --- registry URL/path builders -------------------------------------------

std::string blob_path(std::string_view repository, std::string_view digest);

std::string manifest_path(std::string_view repository,
                          std::string_view reference);

std::string uploads_path(std::string_view repository);

std::string tags_path(std::string_view repository);

std::string referrers_path(std::string_view repository,
                           std::string_view digest);

// resolve the Location header of an upload session (absolute or
// registry-relative) into the URL for the monolithic PUT, appending the
// ?digest= query parameter.
std::string resolve_upload_url(std::string_view registry_url,
                               std::string_view location,
                               std::string_view digest);

// --- response-body parsers -------------------------------------------------

// parse a /v2/<repo>/tags/list response body.
std::optional<std::vector<std::string>> parse_tags_list(std::string_view body);

// parse the "manifests" array of an OCI image index (a Referrers API
// response, or a referrers tag-schema index) into descriptors.
std::optional<std::vector<descriptor>> parse_referrers(std::string_view body);

// --- token handshake helpers -----------------------------------------------
// The bearer-challenge parser lives in src/oci/parse.cpp
// (oci::parse_bearer_challenge).

// build the token endpoint URL from a bearer challenge and the requested
// scopes.
std::string token_url(const bearer_challenge& challenge,
                      const std::vector<std::string>& scopes);

// extract the token from a token endpoint response body ("token" or
// "access_token").
std::optional<std::string> parse_token_response(std::string_view body);

// build a "repository:<repo>:<actions>" scope string.
std::string repository_scope(std::string_view repository,
                             std::string_view actions);

} // namespace detail
} // namespace oci
