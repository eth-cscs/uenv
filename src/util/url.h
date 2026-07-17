#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <util/expected.h>
#include <util/parse.h>

namespace util {

class url;
// the lexer-based parser (src/util/url.cpp) constructs urls directly.
util::expected<url, parse_error> parse_url(std::string_view text);

// The transport a url names.
//
// `none` handles scheme-less registry configuration, e.g.:
// "jfrog.svc.cscs.ch/uenv".
// `other` covers every scheme this tool does not speak
// (file, gopher, dict, ...).
// Parsing accepts all of them, so that a `url` means "a well-formed url"
// and nothing more; deciding that a given scheme is unacceptable belongs to
// whoever is about to fetch it, not to the parser.
enum class url_scheme { none, http, https, other };

// A URL parsed into its RFC-3986 components. A `url` is always syntactically
// valid — it can only be obtained by parsing — so callers never juggle an
// unvalidated string against a real url.
//
// Two syntax-based normalisations are applied on parse, both of which RFC 3986
// declares safe because the components are case-insensitive: the scheme and the
// host are lowercased. Nothing else is touched. In particular percent-encoded
// octets are passed through verbatim (never decoded, never re-encoded) and the
// path keeps its case: repository names and digests travel there and must
// survive byte-exact.
class url {
  public:
    // parse "[scheme://][userinfo@]host[:port][/path][?query][#fragment]". a
    // bare "host/prefix" (no scheme) is accepted, as registry configs are
    // written. a thin forwarder to util::parse_url.
    static util::expected<url, parse_error> parse(std::string_view text);

    // the scheme as a closed set; see url_scheme.
    url_scheme scheme() const;
    // the scheme text, lowercased (empty when the url has no scheme).
    const std::string& scheme_text() const;
    // e.g. "user:pass" (may be empty).
    const std::string& userinfo() const;
    // lowercased; an IPv6 literal keeps its brackets (e.g. "[::1]").
    const std::string& host() const;
    std::optional<std::uint32_t> port() const;
    // includes the leading '/' (may be empty).
    const std::string& path() const;
    // after '?' (may be empty).
    const std::string& query() const;
    // after '#' (may be empty).
    const std::string& fragment() const;

    // re-render the url from its components.
    std::string string() const;

    // "host[:port]", without scheme, userinfo or path. The form credential
    // stores are keyed by.
    std::string host_port() const;

    // scheme://host[:port], with no userinfo, path, query or fragment: the
    // canonical base to build request urls from. There is no trailing '/' to
    // strip, which is the point — it is the invariant that would otherwise be
    // re-established by hand at every use.
    url origin() const;

    // append `path` to this url's path, joined by exactly one '/'. A trailing
    // '/' on `path` is preserved (the OCI base endpoint really is "/v2/").
    //
    // `path` is a path and nothing else: it is appended verbatim, so a '?' or
    // '#' in it lands in the path rather than starting a query or fragment. To
    // graft on a reference that may carry a query, compose the text and parse
    // it (see oci::detail::resolve_upload_url).
    url resolve(std::string_view path) const;

    // append "key=value" to the query, percent-encoding both.
    //
    // The encoder keeps the RFC 3986 unreserved set plus ':' '/' and ',', which
    // is exactly the alphabet of every value uenv sends today (auth scopes,
    // digests, repository paths), so it is a no-op on them and the bytes on the
    // wire are unchanged. What it does escape is '&', '=', '#', '+', space and
    // control characters — the characters that would otherwise let a value
    // supplied by a registry inject query parameters of its own.
    url query_param(std::string_view key, std::string_view value) const;

    friend bool operator==(const url&, const url&) = default;
    friend util::expected<url, parse_error> parse_url(std::string_view);

  private:
    url() = default;

    std::string scheme_;
    std::string userinfo_;
    std::string host_;
    std::optional<std::uint32_t> port_;
    std::string path_;
    std::string query_;
    std::string fragment_;
};

} // namespace util

#include <fmt/core.h>
template <> class fmt::formatter<util::url> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    template <typename FmtContext>
    auto format(util::url const& u, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", u.string());
    }
};
