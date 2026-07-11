#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <oci/auth.h>
#include <oci/digest.h>
#include <oci/reference.h>
#include <oci/tag.h>
#include <util/expected.h>
#include <util/parse.h>

// Lexer-based parsers for the string types used by the OCI client. These mirror
// the interface style of src/uenv/parse.* and share the util parsing
// scaffolding (util::parse_error, the PARSE macro, util::parse_string) so that
// src/oci stays self-contained on src/util. JSON documents are still parsed
// with nlohmann; the parsers here handle the character-level string types only.
namespace oci {

// parse an OCI content digest "<algorithm>:<hex>". rejects an unknown algorithm
// and a value whose length does not match the algorithm or that is not
// lowercase hex.
util::expected<digest, util::parse_error> parse_digest(std::string_view text);

// parse an OCI tag against the tag grammar
// ([a-zA-Z0-9_][a-zA-Z0-9._-]{0,127}). rejects a leading '.' or '-', an empty
// value, an out-of-grammar character, and a value longer than 128 characters.
util::expected<tag, util::parse_error> parse_tag(std::string_view text);

// parse a manifest reference: a valid "<algo>:<hex>" becomes a digest
// reference, otherwise the text is validated against the OCI tag grammar and
// becomes a tag.
util::expected<reference, util::parse_error>
parse_reference(std::string_view text);

// A URL parsed into its RFC-3986 components. `host` retains the surrounding
// brackets for an IPv6 literal (e.g. "[::1]"). Percent-encoded octets are
// passed through verbatim (not decoded).
struct url {
    std::string scheme;   // e.g. "https" (may be empty)
    std::string userinfo; // e.g. "user:pass" (may be empty)
    std::string host;     // e.g. "jfrog.svc.cscs.ch" or "[::1]"
    std::optional<std::uint32_t> port;
    std::string path;     // includes the leading '/' (may be empty)
    std::string query;    // after '?' (may be empty)
    std::string fragment; // after '#' (may be empty)

    // re-render the URL from its components.
    std::string string() const;

    friend bool operator==(const url&, const url&) = default;
};

// parse a URL "[scheme://][userinfo@]host[:port][/path][?query][#fragment]". a
// bare "host/prefix" (no scheme) is accepted, as registry configs are written.
util::expected<url, util::parse_error> parse_url(std::string_view text);

// parse a "WWW-Authenticate: Bearer ..." challenge. the scheme match is
// case-insensitive; `realm` is required.
util::expected<bearer_challenge, util::parse_error>
parse_bearer_challenge(std::string_view text);

// split a scope value on whitespace (the OCI spec permits a single scope
// parameter carrying a space-separated list). never fails.
std::vector<std::string> parse_scopes(std::string_view value);

} // namespace oci
