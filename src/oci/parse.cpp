#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <oci/parse.h>
#include <util/lex.h>
#include <util/parse.h>
#include <util/sha.h>
#include <util/strings.h>
#include <util/url.h>

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
    if (auto e = util::expect_end(L, "digest"); !e) {
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

util::expected<tag, util::parse_error> parse_tag(std::string_view text) {
    const auto s = util::strip(text);
    lex::lexer L(s);

    const auto start = L.peek();
    // the OCI tag grammar forbids a leading '.' or '-'.
    if (L != lex::tok::symbol && L != lex::tok::integer) {
        return util::unexpected(parse_error{
            L.string(), fmt::format("'{}' is not a valid tag", s), start});
    }
    auto value = util::parse_string(L, "tag", is_tag_tok);
    if (!value) {
        return util::unexpected(value.error());
    }
    if (auto e = util::expect_end(L, "tag"); !e) {
        return util::unexpected(e.error());
    }
    if (value->size() > 128) {
        return util::unexpected(parse_error{
            L.string(), "a tag may be at most 128 characters", start});
    }
    // parse_tag is a friend of tag, so it can use the private constructor.
    return tag{std::move(*value)};
}

util::expected<reference, util::parse_error>
parse_reference(std::string_view text) {
    // a digest is unambiguous (it contains ':'); try it first.
    if (auto d = parse_digest(text)) {
        return reference::digest(*d);
    }

    auto t = parse_tag(text);
    if (!t) {
        return util::unexpected(t.error());
    }
    return reference::tag(std::move(*t));
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

    // the fields are collected into locals and the challenge is built at the
    // end, once the realm has parsed: a bearer_challenge holds a util::url,
    // which cannot be default-constructed - there is no "empty url" to stand in
    // for "not seen yet".
    std::optional<std::string> realm_text;
    std::string service;
    std::vector<std::string> scopes;
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
            realm_text = std::move(value);
        } else if (key == "service") {
            service = std::move(value);
        } else if (key == "scope") {
            auto parsed = parse_scopes(value);
            scopes.insert(scopes.end(), parsed.begin(), parsed.end());
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

    if (!realm_text) {
        return util::unexpected(parse_error{
            full, "the Bearer challenge is missing the required 'realm'",
            scheme_tok});
    }
    // the realm is a url supplied by the registry: parse it here, at the
    // boundary, rather than letting a malformed one travel as a string.
    auto realm = util::parse_url(*realm_text);
    if (!realm) {
        return util::unexpected(parse_error{
            full,
            fmt::format("the Bearer challenge realm '{}' is not a valid url",
                        *realm_text),
            scheme_tok});
    }
    return bearer_challenge{.realm = std::move(*realm),
                            .service = std::move(service),
                            .scopes = std::move(scopes)};
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
