#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <util/lex.h>
#include <util/parse.h>
#include <util/strings.h>
#include <util/url.h>

namespace util {

namespace {

//
// token predicates
//

// the leading token of a URL scheme (ALPHA), and the continuation tokens
// (ALPHA / DIGIT / "+" / "-" / ".").
bool is_scheme_tok(lex::tok t) {
    return t == lex::tok::symbol || t == lex::tok::integer ||
           t == lex::tok::plus || t == lex::tok::dash || t == lex::tok::dot;
}

// a URL userinfo component (unreserved / pct-encoded / sub-delims / ":"),
// RFC 3986 §3.2.1. anything else before the '@' (whitespace, an invalid byte,
// a '/') means there is no userinfo.
bool is_userinfo_tok(lex::tok t) {
    switch (t) {
    case lex::tok::symbol:
    case lex::tok::integer:
    case lex::tok::dash:
    case lex::tok::dot:
    case lex::tok::tilde:
    case lex::tok::percent:
    case lex::tok::colon:
    case lex::tok::bang:
    case lex::tok::dollar:
    case lex::tok::amp:
    case lex::tok::squote:
    case lex::tok::lparen:
    case lex::tok::rparen:
    case lex::tok::star:
    case lex::tok::plus:
    case lex::tok::comma:
    case lex::tok::semicolon:
    case lex::tok::equals:
        return true;
    default:
        return false;
    }
}

// a URL reg-name host component (unreserved + pct-encoded); we stop at ':',
// '/', '?', '#', whitespace or end.
bool is_regname_tok(lex::tok t) {
    return t == lex::tok::symbol || t == lex::tok::integer ||
           t == lex::tok::dash || t == lex::tok::dot || t == lex::tok::tilde ||
           t == lex::tok::percent;
}

// the body of an IPv6 literal between '[' and ']'.
bool is_ipv6_tok(lex::tok t) {
    return t == lex::tok::symbol || t == lex::tok::integer ||
           t == lex::tok::colon || t == lex::tok::dot || t == lex::tok::percent;
}

// a URL path segment run (pchar plus '/'): everything up to a query, fragment,
// whitespace or end.
bool is_path_tok(lex::tok t) {
    switch (t) {
    case lex::tok::question:
    case lex::tok::hash:
    case lex::tok::whitespace:
    case lex::tok::end:
    case lex::tok::error:
        return false;
    default:
        return true;
    }
}

// a query run: everything up to a fragment, whitespace or end.
bool is_query_tok(lex::tok t) {
    switch (t) {
    case lex::tok::hash:
    case lex::tok::whitespace:
    case lex::tok::end:
    case lex::tok::error:
        return false;
    default:
        return true;
    }
}

// a fragment run: everything up to whitespace or end.
bool is_fragment_tok(lex::tok t) {
    switch (t) {
    case lex::tok::whitespace:
    case lex::tok::end:
    case lex::tok::error:
        return false;
    default:
        return true;
    }
}

// percent-encode `s` for use as a query key or value. See url::query_param for
// why the keep-set is what it is.
std::string encode_query(std::string_view s) {
    auto keep = [](unsigned char c) {
        // RFC 3986 unreserved.
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
            return true;
        }
        switch (c) {
        case '-':
        case '.':
        case '_':
        case '~':
        // legal in a query and load-bearing in the values uenv already sends:
        // "repository:<repo>:pull,push", "sha256:<hex>", "a/b/c".
        case ':':
        case '/':
        case ',':
            return true;
        default:
            return false;
        }
    };

    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (keep(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            fmt::format_to(std::back_inserter(out), "%{:02X}", c);
        }
    }
    return out;
}

} // namespace

//
// url
//

util::expected<url, parse_error> url::parse(std::string_view text) {
    return parse_url(text);
}

url_scheme url::scheme() const {
    if (scheme_.empty()) {
        return url_scheme::none;
    }
    if (scheme_ == "https") {
        return url_scheme::https;
    }
    if (scheme_ == "http") {
        return url_scheme::http;
    }
    return url_scheme::other;
}

const std::string& url::scheme_text() const {
    return scheme_;
}

const std::string& url::userinfo() const {
    return userinfo_;
}

const std::string& url::host() const {
    return host_;
}

std::optional<std::uint32_t> url::port() const {
    return port_;
}

const std::string& url::path() const {
    return path_;
}

const std::string& url::query() const {
    return query_;
}

const std::string& url::fragment() const {
    return fragment_;
}

std::string url::string() const {
    std::string out;
    if (!scheme_.empty()) {
        out += scheme_;
        out += "://";
    }
    if (!userinfo_.empty()) {
        out += userinfo_;
        out += '@';
    }
    out += host_port();
    out += path_;
    if (!query_.empty()) {
        out += '?';
        out += query_;
    }
    if (!fragment_.empty()) {
        out += '#';
        out += fragment_;
    }
    return out;
}

std::string url::host_port() const {
    std::string out = host_;
    if (port_) {
        out += ':';
        out += std::to_string(*port_);
    }
    return out;
}

url url::origin() const {
    url out;
    out.scheme_ = scheme_;
    out.host_ = host_;
    out.port_ = port_;
    return out;
}

url url::resolve(std::string_view path) const {
    url out = *this;
    if (path.empty()) {
        return out;
    }
    const bool have = !out.path_.empty() && out.path_.back() == '/';
    const bool given = path.front() == '/';
    if (have && given) {
        out.path_.pop_back(); // exactly one '/' at the join
    } else if (!have && !given) {
        out.path_.push_back('/');
    }
    out.path_ += path;
    return out;
}

url url::query_param(std::string_view key, std::string_view value) const {
    url out = *this;
    if (!out.query_.empty()) {
        out.query_ += '&';
    }
    out.query_ += encode_query(key);
    out.query_ += '=';
    out.query_ += encode_query(value);
    return out;
}

util::expected<url, parse_error> parse_url(std::string_view text) {
    const auto s = util::strip(text);
    lex::lexer L(s);
    url u;

    // optional scheme: <scheme> "://". consume a scheme-shaped run, and only
    // accept it as a scheme if it is followed by "://"; otherwise rewind (a
    // bare "host/prefix" or "host:port" begins with the same tokens).
    {
        const unsigned start = L.peek().loc;
        std::string scheme;
        while (is_scheme_tok(L.current_kind())) {
            scheme += L.next().spelling;
        }
        // a scheme starts with a letter (RFC 3986 §3.1): "1://h" has no
        // scheme, and is rejected below because "1:" is not a host.
        const bool scheme_ok =
            !scheme.empty() && ((scheme[0] >= 'a' && scheme[0] <= 'z') ||
                                (scheme[0] >= 'A' && scheme[0] <= 'Z'));
        if (scheme_ok && L == lex::tok::colon && L.peek(1) == lex::tok::slash &&
            L.peek(2) == lex::tok::slash) {
            L.next(); // ':'
            L.next(); // '/'
            L.next(); // '/'
            // schemes are case-insensitive (RFC 3986 §3.1): lowercase so that
            // "HTTPS://" is recognised as https rather than an unknown scheme.
            u.scheme_ = util::to_lower(scheme);
        } else {
            L.seek(start);
        }
    }

    // optional userinfo: present only when an '@' occurs before the first
    // token that cannot be part of a userinfo ('/', '?', '#', whitespace, an
    // invalid byte, or the end). the candidate is consumed and the lexer
    // rewound if no '@' follows: peeking further ahead on every step would
    // re-lex from the current position each time, which is quadratic in the
    // length of the url.
    {
        const unsigned start = L.peek().loc;
        std::string userinfo;
        while (is_userinfo_tok(L.current_kind())) {
            userinfo += L.next().spelling;
        }
        if (L == lex::tok::at) {
            L.next(); // consume '@'
            u.userinfo_ = std::move(userinfo);
        } else {
            L.seek(start);
        }
    }

    // host: an IPv6 literal in brackets, or a reg-name.
    const auto host_tok = L.peek();
    if (L == lex::tok::lbracket) {
        std::string host = std::string{L.next().spelling}; // '['
        while (is_ipv6_tok(L.current_kind())) {
            host += L.next().spelling;
        }
        if (L != lex::tok::rbracket) {
            return util::unexpected(parse_error{
                L.string(), "unterminated IPv6 host literal", host_tok});
        }
        host += L.next().spelling; // ']'
        u.host_ = util::to_lower(host);
    } else {
        auto host = util::parse_string(L, "host", is_regname_tok);
        if (!host) {
            return util::unexpected(
                parse_error{L.string(), "the url has no host", host_tok});
        }
        // hosts are case-insensitive (RFC 3986 §3.2.2).
        u.host_ = util::to_lower(*host);
    }

    // optional port.
    if (L == lex::tok::colon) {
        L.next(); // consume ':'
        const auto port_tok = L.peek();
        if (L != lex::tok::integer) {
            return util::unexpected(parse_error{
                L.string(), "expected a port number after ':'", port_tok});
        }
        const auto digits = L.next().spelling;
        std::uint32_t port = 0;
        const auto* first = digits.data();
        const auto* last = digits.data() + digits.size();
        if (auto [ptr, ec] = std::from_chars(first, last, port);
            ec != std::errc{} || ptr != last || port > 65535) {
            return util::unexpected(
                parse_error{L.string(), "invalid port number", port_tok});
        }
        u.port_ = port;
    }

    // optional path (begins with '/').
    if (L == lex::tok::slash) {
        auto path = util::parse_string(L, "path", is_path_tok);
        u.path_ = std::move(*path); // starts with '/', so never empty
    }

    // optional query.
    if (L == lex::tok::question) {
        L.next(); // consume '?'
        if (is_query_tok(L.current_kind())) {
            auto query = util::parse_string(L, "query", is_query_tok);
            u.query_ = std::move(*query);
        }
    }

    // optional fragment.
    if (L == lex::tok::hash) {
        L.next(); // consume '#'
        if (is_fragment_tok(L.current_kind())) {
            auto fragment = util::parse_string(L, "fragment", is_fragment_tok);
            u.fragment_ = std::move(*fragment);
        }
    }

    if (auto e = util::expect_end(L, "url"); !e) {
        return util::unexpected(e.error());
    }
    return u;
}

} // namespace util
