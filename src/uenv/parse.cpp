#include <string>
#include <string_view>
#include <vector>

#include <util/expected.h>

#include <uenv/env.h>
#include <uenv/lex.h>
#include <uenv/parse.h>

namespace uenv {

// some pre-processor gubbins that generates code to attempt
// parsing a value using a parse_x method. unwraps and
// forwards the error if there was an error.
// much ergonomics!
#define PARSE(L, TYPE, X)                                                      \
    {                                                                          \
        if (auto rval__ = parse_##TYPE(L))                                     \
            X = *rval__;                                                       \
        else                                                                   \
            return util::unexpected(std::move(rval__.error()));                \
    }

template <typename Test>
util::expected<std::string, parse_error>
parse_string(lexer& L, std::string_view type, Test&& test) {
    std::string result;
    while (test(L.current_kind())) {
        const auto t = L.next();
        result += t.spelling;
    }

    // if result is empty, nothing was parsed
    if (result.empty()) {
        const auto t = L.peek();
        return util::unexpected(parse_error{
            fmt::format("internal error parsing a {}, unexpected '{}'", type,
                        t.spelling),
            t.loc});
    }

    return result;
}

// strip all tabs, newlines, cariage returns, etc from an input string.
std::string sanitise_input(std::string_view input) {
    std::string sanitised;
    sanitised.reserve(input.size());

    for (auto c : input) {
        if (c == '\n' || c == '\t' || c == '\v' || c == '\r') {
            continue;
        }
        sanitised.push_back(c);
    }
    return sanitised;
}

bool is_name_tok(tok t) {
    return t == tok::symbol || t == tok::dash || t == tok::dot;
};
util::expected<std::string, parse_error> parse_name(lexer& L) {
    return parse_string(L, "name", is_name_tok);
}

bool is_path_tok(tok t) {
    return t == tok::slash || t == tok::symbol || t == tok::dash ||
           t == tok::dot;
};
util::expected<std::string, parse_error> parse_path(lexer& L) {
    return parse_string(L, "path", is_path_tok);
}

util::expected<view_description, parse_error> parse_view_description(lexer& L) {
    // there are two valid inputs to parse
    // name1            ->  uenv=none,  name=name1
    // name1:name2      ->  uenv=name1, name=name2
    std::string name1;
    PARSE(L, name, name1);
    if (L.current_kind() == tok::colon) {
        // flush the :
        L.next();
        std::string name2;
        PARSE(L, name, name2);
        return view_description{name1, name2};
    }

    return view_description{{}, name1};
}

util::expected<uenv_label, parse_error> parse_uenv_label(lexer& L) {
    uenv_label result;

    PARSE(L, name, result.name);
    if (L.current_kind() == tok::slash) {
        L.next();
        PARSE(L, name, result.version);
    }
    if (L.current_kind() == tok::colon) {
        L.next();
        PARSE(L, name, result.tag);
    }

    return result;
}

// parse an individual uenv description
util::expected<uenv_description, parse_error> parse_uenv_description(lexer& L) {
    const auto k = L.current_kind();

    // try to parse a path
    if (k == tok::slash) {
        std::string path;
        PARSE(L, path, path);
        if (L.current_kind() == tok::at) {
            L.next();
            std::string mount;
            PARSE(L, path, mount);
            return uenv_description{path, mount};
        } else {
            return uenv_description{path};
        }
    }

    // try to parse a label
    if (is_name_tok(k)) {
        uenv_label label;
        PARSE(L, uenv_label, label);
        if (L.current_kind() == tok::at) {
            L.next();
            std::string mount;
            PARSE(L, path, mount);
            return uenv_description{label, mount};
        } else {
            return uenv_description{label};
        }
    }

    // neither path nor name label - oops
    const auto t = L.peek();
    return util::unexpected(parse_error{
        fmt::format("not a valid uenv description, unexpected symbol '{}'",
                    t.spelling),
        t.loc});
}

/* Public interface.
 * These are the high level functions for parsing raw strings passed to the
 * command line.
 */

// generate a list of view descriptions from a CLI argument,
// e.g. prgenv-gnu:spack,modules
util::expected<std::vector<view_description>, parse_error>
parse_view_args(const std::string& arg) {
    const std::string sanitised = sanitise_input(arg);
    auto L = lexer(sanitised);
    std::vector<view_description> views;

    while (true) {
        view_description v;
        PARSE(L, view_description, v);
        views.push_back(std::move(v));

        if (L.current_kind() != tok::comma) {
            break;
        }
        // eat the comma
        L.next();

        // handle trailing comma elegantly
        if (L.peek().kind == tok::end) {
            break;
        }
    }
    // if parsing finished and the string has not been
    // consumed, and invalid token was encountered
    if (const auto t = L.peek(); t.kind != tok::end) {
        return util::unexpected(parse_error{
            fmt::format("unexpected symbol {}", t.spelling), t.loc});
    }

    return views;
}
// parse a comma-separated list of uenv descriptions
util::expected<std::vector<uenv_description>, parse_error>
parse_uenv_args(const std::string& arg) {
    const std::string sanitised = sanitise_input(arg);
    auto L = lexer(sanitised);
    std::vector<uenv_description> uenvs;

    while (true) {
        uenv_description desc;
        PARSE(L, uenv_description, desc);
        uenvs.push_back(std::move(desc));

        if (L.peek().kind != tok::comma) {
            break;
        }
        // eat the comma
        L.next();

        // handle trailing comma elegantly
        if (L.peek().kind == tok::end) {
            break;
        }
    }
    // if parsing finished and the string has not been consumed,
    // and invalid token was encountered
    if (const auto t = L.peek(); t.kind != tok::end) {
        return util::unexpected(parse_error{
            fmt::format("unexpected symbol {}", t.spelling), t.loc});
    }
    return uenvs;
}

} // namespace uenv
