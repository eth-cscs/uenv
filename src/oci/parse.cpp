#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <oci/parse.h>
#include <util/lex.h>
#include <util/parse.h>
#include <util/strings.h>

namespace oci {

// the parsing scaffolding lives in util; use the unqualified name here (as
// src/uenv/parse.cpp does) so the parse_error{...} constructions read cleanly.
using util::parse_error;

namespace {

// --- token predicates ---------------------------------------------------

// alphanumeric run: the lexer splits e.g. "sha256" into symbol("sha") +
// integer("256") and a hex string into alternating symbol/integer tokens, so
// both a digest algorithm and its hex value are runs of these two kinds.
bool is_alnum_tok(lex::tok t) {
    return t == lex::tok::symbol || t == lex::tok::integer;
}

// OCI tag grammar body: [a-zA-Z0-9_][a-zA-Z0-9._-]{0,127}. underscore is part
// of a symbol token.
bool is_tag_tok(lex::tok t) {
    return t == lex::tok::symbol || t == lex::tok::integer ||
           t == lex::tok::dot || t == lex::tok::dash;
}

// the leading token of a URL scheme (ALPHA), and the continuation tokens
// (ALPHA / DIGIT / "+" / "-" / ".").
bool is_scheme_tok(lex::tok t) {
    return t == lex::tok::symbol || t == lex::tok::integer ||
           t == lex::tok::plus || t == lex::tok::dash || t == lex::tok::dot;
}

// a URL reg-name host component (unreserved + pct-encoded); we stop at ':',
// '/',
// '?', '#', whitespace or end.
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

// an unquoted auth-parameter value: everything up to a ',' or end.
bool is_unquoted_value_tok(lex::tok t) {
    return t != lex::tok::comma && t != lex::tok::end;
}

// the hex length expected for a recognised algorithm, or 0 if unrecognised.
std::size_t hex_length(std::string_view algorithm) {
    if (algorithm == "sha256") {
        return 64;
    }
    if (algorithm == "sha512") {
        return 128;
    }
    return 0;
}

bool is_lower_hex(std::string_view s) {
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

// require the lexer to be at end-of-input, otherwise build a "trailing input"
// parse_error for `what`.
util::expected<void, util::parse_error> expect_end(lex::lexer& L,
                                                   std::string_view what) {
    if (L != lex::tok::end) {
        const auto t = L.peek();
        return util::unexpected(parse_error{
            L.string(), fmt::format("unexpected '{}' in {}", t.spelling, what),
            t});
    }
    return {};
}

} // namespace

util::expected<digest, util::parse_error> parse_digest(std::string_view text) {
    const auto s = util::strip(text);
    lex::lexer L(s);

    const auto algo_tok = L.peek();
    auto algorithm = util::parse_string(L, "digest algorithm", is_alnum_tok);
    if (!algorithm) {
        return util::unexpected(algorithm.error());
    }
    if (L != lex::tok::colon) {
        const auto t = L.peek();
        return util::unexpected(parse_error{
            L.string(), "expected ':' separating the algorithm and value", t});
    }
    L.next(); // consume ':'

    const auto hex_tok = L.peek();
    auto hex = util::parse_string(L, "digest value", is_alnum_tok);
    if (!hex) {
        return util::unexpected(hex.error());
    }
    if (auto e = expect_end(L, "digest"); !e) {
        return util::unexpected(e.error());
    }

    const auto want = hex_length(*algorithm);
    if (want == 0) {
        return util::unexpected(parse_error{
            L.string(),
            fmt::format("unsupported digest algorithm '{}'", *algorithm),
            algo_tok});
    }
    if (hex->size() != want) {
        return util::unexpected(
            parse_error{L.string(),
                        fmt::format("expected {} hex characters, got {}", want,
                                    hex->size()),
                        hex_tok});
    }
    if (!is_lower_hex(*hex)) {
        return util::unexpected(parse_error{
            L.string(), "the digest value is not lowercase hex", hex_tok});
    }

    // parse_digest is a friend of digest, so it can use the private
    // constructor.
    return digest{std::move(*algorithm), std::move(*hex)};
}

util::expected<reference, util::parse_error>
parse_reference(std::string_view text) {
    // a digest is unambiguous (it contains ':'); try it first.
    if (auto d = parse_digest(text)) {
        return reference::digest(*d);
    }

    const auto s = util::strip(text);
    lex::lexer L(s);

    const auto start = L.peek();
    // the OCI tag grammar forbids a leading '.' or '-'.
    if (L != lex::tok::symbol && L != lex::tok::integer) {
        return util::unexpected(parse_error{
            L.string(),
            fmt::format("'{}' is not a valid tag or digest reference", s),
            start});
    }
    auto tag = util::parse_string(L, "tag", is_tag_tok);
    if (!tag) {
        return util::unexpected(tag.error());
    }
    if (auto e = expect_end(L, "reference"); !e) {
        return util::unexpected(e.error());
    }
    if (tag->size() > 128) {
        return util::unexpected(parse_error{
            L.string(), "a tag may be at most 128 characters", start});
    }
    return reference::tag(std::move(*tag));
}

std::string url::string() const {
    std::string out;
    if (!scheme.empty()) {
        out += scheme;
        out += "://";
    }
    if (!userinfo.empty()) {
        out += userinfo;
        out += '@';
    }
    out += host;
    if (port) {
        out += ':';
        out += std::to_string(*port);
    }
    out += path;
    if (!query.empty()) {
        out += '?';
        out += query;
    }
    if (!fragment.empty()) {
        out += '#';
        out += fragment;
    }
    return out;
}

util::expected<url, util::parse_error> parse_url(std::string_view text) {
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
        if (!scheme.empty() && L == lex::tok::colon &&
            L.peek(1) == lex::tok::slash && L.peek(2) == lex::tok::slash) {
            L.next(); // ':'
            L.next(); // '/'
            L.next(); // '/'
            u.scheme = std::move(scheme);
        } else {
            L.seek(start);
        }
    }

    // optional userinfo: present only when an '@' occurs before the first
    // '/', '?', '#' or end.
    {
        bool has_userinfo = false;
        for (unsigned k = 0;; ++k) {
            const auto t = L.peek(k);
            if (t == lex::tok::slash || t == lex::tok::question ||
                t == lex::tok::hash || t == lex::tok::end) {
                break;
            }
            if (t == lex::tok::at) {
                has_userinfo = true;
                break;
            }
        }
        if (has_userinfo) {
            std::string userinfo;
            while (L != lex::tok::at) {
                userinfo += L.next().spelling;
            }
            L.next(); // consume '@'
            u.userinfo = std::move(userinfo);
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
        u.host = std::move(host);
    } else {
        auto host = util::parse_string(L, "host", is_regname_tok);
        if (!host) {
            return util::unexpected(
                parse_error{L.string(), "the url has no host", host_tok});
        }
        u.host = std::move(*host);
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
            ec != std::errc{} || ptr != last) {
            return util::unexpected(
                parse_error{L.string(), "invalid port number", port_tok});
        }
        u.port = port;
    }

    // optional path (begins with '/').
    if (L == lex::tok::slash) {
        auto path = util::parse_string(L, "path", is_path_tok);
        u.path = std::move(*path); // starts with '/', so never empty
    }

    // optional query.
    if (L == lex::tok::question) {
        L.next(); // consume '?'
        if (is_query_tok(L.current_kind())) {
            auto query = util::parse_string(L, "query", is_query_tok);
            u.query = std::move(*query);
        }
    }

    // optional fragment.
    if (L == lex::tok::hash) {
        L.next(); // consume '#'
        if (is_fragment_tok(L.current_kind())) {
            auto fragment = util::parse_string(L, "fragment", is_fragment_tok);
            u.fragment = std::move(*fragment);
        }
    }

    if (auto e = expect_end(L, "url"); !e) {
        return util::unexpected(e.error());
    }
    return u;
}

util::expected<bearer_challenge, util::parse_error>
parse_bearer_challenge(std::string_view text) {
    const auto full = util::trim(text);
    lex::lexer L(full);

    // scheme: a case-insensitive "Bearer" token. "Bearerish" lexes as a single
    // symbol and so is rejected here; "Basic" likewise.
    const auto scheme_tok = L.peek();
    if (L != lex::tok::symbol ||
        util::to_lower(scheme_tok.spelling) != "bearer") {
        return util::unexpected(parse_error{
            full, "expected a 'Bearer' authentication scheme", scheme_tok});
    }
    L.next(); // consume the scheme
    if (L != lex::tok::whitespace && L != lex::tok::end) {
        const auto t = L.peek();
        return util::unexpected(parse_error{
            full, "expected whitespace after the 'Bearer' scheme", t});
    }
    if (L == lex::tok::whitespace) {
        L.next();
    }

    bearer_challenge challenge;
    while (L != lex::tok::end) {
        // parameter name.
        const auto key_tok = L.peek();
        if (L != lex::tok::symbol) {
            return util::unexpected(
                parse_error{full, "expected a parameter name", key_tok});
        }
        const std::string key = util::to_lower(L.next().spelling);

        if (L == lex::tok::whitespace) {
            L.next();
        }
        if (L != lex::tok::equals) {
            const auto t = L.peek();
            return util::unexpected(
                parse_error{full, "expected '=' after the parameter name", t});
        }
        L.next(); // consume '='
        if (L == lex::tok::whitespace) {
            L.next();
        }

        // parameter value: a quoted string (whose content may contain any byte,
        // so it is read raw rather than tokenised) or a bare token run.
        std::string value;
        if (L == lex::tok::dquote) {
            const unsigned q = L.peek().loc; // position of the opening '"'
            std::size_t i = static_cast<std::size_t>(q) + 1;
            std::string buf;
            bool closed = false;
            while (i < full.size()) {
                const char c = full[i];
                if (c == '\\' && i + 1 < full.size()) {
                    buf.push_back(full[i + 1]);
                    i += 2;
                    continue;
                }
                if (c == '"') {
                    closed = true;
                    break;
                }
                buf.push_back(c);
                ++i;
            }
            if (!closed) {
                return util::unexpected(parse_error{
                    full, "unterminated quoted parameter value", L.peek()});
            }
            L.seek(static_cast<unsigned>(i + 1)); // skip past the closing '"'
            value = std::move(buf);
        } else {
            auto raw = util::parse_string(L, "value", is_unquoted_value_tok);
            if (!raw) {
                return util::unexpected(raw.error());
            }
            value = std::string{util::trim(*raw)};
        }

        if (key == "realm") {
            challenge.realm = std::move(value);
        } else if (key == "service") {
            challenge.service = std::move(value);
        } else if (key == "scope") {
            auto scopes = parse_scopes(value);
            challenge.scopes.insert(challenge.scopes.end(), scopes.begin(),
                                    scopes.end());
        }
        // unknown parameters are ignored.

        // separator: a comma between parameters, or end.
        if (L == lex::tok::whitespace) {
            L.next();
        }
        if (L == lex::tok::comma) {
            L.next();
            if (L == lex::tok::whitespace) {
                L.next();
            }
        } else if (L != lex::tok::end) {
            const auto t = L.peek();
            return util::unexpected(
                parse_error{full, "expected ',' between parameters", t});
        }
    }

    if (challenge.realm.empty()) {
        return util::unexpected(parse_error{
            full, "the Bearer challenge is missing the required 'realm'",
            scheme_tok});
    }
    return challenge;
}

std::vector<std::string> parse_scopes(std::string_view value) {
    std::vector<std::string> out;
    lex::lexer L(value);
    std::string current;
    auto flush = [&] {
        if (!current.empty()) {
            out.push_back(std::move(current));
            current.clear();
        }
    };
    while (L != lex::tok::end) {
        if (L == lex::tok::whitespace) {
            L.next();
            flush();
        } else {
            current += L.next().spelling;
        }
    }
    flush();
    return out;
}

} // namespace oci
