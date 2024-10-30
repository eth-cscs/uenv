#include <charconv>
#include <string>
#include <string_view>
#include <vector>

#include <util/expected.h>

#include <uenv/env.h>
#include <uenv/lex.h>
#include <uenv/log.h>
#include <uenv/parse.h>

namespace uenv {

std::string parse_error::message() const {
    return fmt::format("{}\n  {}\n  {}{}", detail, input, std::string(loc, ' '),
                       std::string(std::max(1u, width), '^'));
}

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
            L.string(), fmt::format("unexpected '{}'", type, t.spelling), t});
    }

    return result;
}

// strip all tabs, newlines, cariage returns, etc from an input string.
std::string strip(std::string_view input) {
    if (input.empty()) {
        return {};
    }
    auto b = std::find_if_not(input.begin(), input.end(), std::iswspace);
    if (b == input.end()) {
        return {};
    }
    auto e = input.end();
    while (std::iswspace(*--e))
        ;
    return {b, e + 1};
}

// tokens that can appear in names
// names are used for uenv names, versions, tags
bool is_name_tok(tok t) {
    return t == tok::symbol || t == tok::dash || t == tok::dot ||
           t == tok::integer;
};

// tokens that can be the first token in a name
// don't allow leading dashes and periods
bool is_name_start_tok(tok t) {
    return t == tok::symbol || t == tok::integer;
};

util::expected<std::string, parse_error> parse_name(lexer& L) {
    if (!is_name_start_tok(L.current_kind())) {
        const auto t = L.peek();
        return util::unexpected(parse_error{
            L.string(), fmt::format("found unexpected '{}'", t.spelling), t});
    }
    return parse_string(L, "name", is_name_tok);
}

util::expected<std::uint64_t, parse_error> parse_uint64(lexer& L) {
    const auto t = L.peek();
    if (t.kind != tok::integer) {
        return util::unexpected(parse_error{
            L.string(), fmt::format("{} is not an integer", t.spelling), t});
    }

    auto s = t.spelling;
    std::uint64_t value;
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);

    if (result.ec != std::errc{}) {
        return util::unexpected(
            parse_error{L.string(),
                        fmt::format("{} is not an integer '{}'", s,
                                    std::make_error_code(result.ec).message()),
                        t});
    }

    L.next();

    return value;
}

// all of the symbols that can occur in a path.
// this is a subset of the characters that posix allows (which is effectively
// every character). But it is a sane subset. If the user somehow has spaces or
// colons in their file names, we are doing them a favor.
bool is_path_tok(tok t) {
    return t == tok::slash || t == tok::symbol || t == tok::dash ||
           t == tok::dot || t == tok::integer;
};
// require that all paths start with a dot or /
bool is_path_start_tok(tok t) {
    return t == tok::slash || t == tok::dot;
};

util::expected<std::string, parse_error> parse_path(lexer& L) {
    if (!is_path_start_tok(L.current_kind())) {
        const auto t = L.peek();
        return util::unexpected(parse_error{
            L.string(),
            fmt::format("expected a path which must start with a '/' or '.'"),
            t});
    }
    return parse_string(L, "path", is_path_tok);
}

util::expected<std::string, parse_error> parse_path(const std::string& in) {
    auto L = lexer(in);
    auto result = parse_path(L);
    if (!result) {
        return result;
    }

    if (const auto t = L.peek(); t.kind != tok::end) {
        return util::unexpected(parse_error{
            L.string(), fmt::format("unexpected symbol '{}'", t.spelling), t});
    }
    return result;
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

    // labels are of the form:
    // name[/version][:tag][@system][%uarch]
    // - name is required
    // - the other fields are optional
    // - version and tag are required to be in order (tag after version and
    // before uarch or system)
    // - uarch and system come after tag, and can be in any order

    PARSE(L, name, result.name);
    // process version and tag in order, if they are present
    if (L.current_kind() == tok::slash) {
        L.next();
        PARSE(L, name, result.version);
    }
    if (L.current_kind() == tok::colon) {
        // the ':' character is also used to set the mount point.
        // do not continue parsing if a path follows the ':'.
        if (is_path_start_tok(L.peek(1).kind)) {
            return result;
        }

        L.next();
        PARSE(L, name, result.tag);
    }
    // process version and system in any order
    bool system = false;
    bool uarch = false;
    while ((L.current_kind() == tok::at && !system) ||
           (L.current_kind() == tok::percent && !uarch)) {
        if (L.current_kind() == tok::at) {
            L.next();
            if (L.current_kind() == tok::star) {
                result.system = "*";
                L.next();
            } else {
                PARSE(L, name, result.system);
            }
            system = true;
        } else if (L.current_kind() == tok::percent) {
            L.next();
            PARSE(L, name, result.uarch);
            uarch = true;
        }
    }

    return result;
}

util::expected<uenv_label, parse_error>
parse_uenv_label(const std::string& in) {
    auto L = lexer(in);
    auto result = parse_uenv_label(L);
    if (!result) {
        return result;
    }

    if (const auto t = L.peek(); t.kind != tok::end) {
        return util::unexpected(parse_error{
            L.string(), fmt::format("unexpected symbol '{}'", t.spelling), t});
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
        if (L.current_kind() == tok::colon) {
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
        if (L.current_kind() == tok::colon) {
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
        L.string(), fmt::format("unexpected symbol '{}'", t.spelling), t});
}

util::expected<mount_entry, parse_error> parse_mount_entry(lexer& L) {
    mount_entry result;

    PARSE(L, path, result.sqfs_path);
    if (L.current_kind() != tok::colon) {
        const auto t = L.peek();
        return util::unexpected(parse_error(
            L.string(),
            fmt::format("expected a ':' separating the squashfs image and "
                        "mount path, found '{}'",
                        t.spelling),
            t));
    }
    // eat the colon
    L.next();
    PARSE(L, path, result.mount_path);

    return result;
}

/* Public interface.
 * These are the high level functions for parsing raw strings passed to the
 * command line.
 */

// generate a list of view descriptions from a CLI argument,
// e.g. prgenv-gnu:spack,modules
util::expected<std::vector<view_description>, parse_error>
parse_view_args(const std::string& arg) {
    spdlog::trace("parsing view args {}", arg);

    const std::string sanitised = strip(arg);
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
            L.string(), fmt::format("unexpected symbol '{}'", t.spelling), t});
    }

    return views;
}
// parse a comma-separated list of uenv descriptions
util::expected<std::vector<uenv_description>, parse_error>
parse_uenv_args(const std::string& arg) {
    spdlog::trace("parsing uenv args {}", arg);

    const std::string sanitised = strip(arg);
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
            L.string(), fmt::format("unexpected symbol '{}'", t.spelling), t});
    }
    return uenvs;
}

util::expected<std::vector<mount_entry>, parse_error>
parse_mount_list(const std::string& arg) {
    const std::string sanitised = strip(arg);
    auto L = lexer(sanitised);
    std::vector<mount_entry> mounts;

    spdlog::trace("parsing uenv mount list {}", arg);
    while (true) {
        mount_entry mnt;
        PARSE(L, mount_entry, mnt);
        mounts.push_back(std::move(mnt));

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
            L.string(), fmt::format("unexpected symbol {}", t.spelling), t});
    }
    return mounts;
}

// yyyy.mm.dd [hh:mm:ss]
// 2024.12.10
// 2025.1.24
// 2025.01.24
// 2023.07.16 21:13:06
util::expected<uenv_date, parse_error> parse_uenv_date(const std::string& arg) {
    const std::string sanitised = strip(arg);
    auto L = lexer(sanitised);
    uenv_date date;

    spdlog::trace("parsing uenv_date {}", arg);

    PARSE(L, uint64, date.year);
    if (L.peek().kind != tok::dash) {
        goto unexpected_symbol;
    }
    L.next();
    PARSE(L, uint64, date.month);
    if (L.peek().kind != tok::dash) {
        goto unexpected_symbol;
    }
    L.next();
    PARSE(L, uint64, date.day);

    // time to finish - date was in 'yyyy-mm-dd' format
    if (L.peek().kind == tok::end) {
        goto validate_and_return;
    }

    // continue processing on the assumption that the date is in the
    // 'yyyy-mm-dd hh:mm:ss.uuuuuu+hh:mm' format
    if (L.peek().kind != tok::whitespace) {
        goto unexpected_symbol;
    }
    // eat whitespace
    L.next();

    PARSE(L, uint64, date.hour);
    if (L.peek().kind != tok::colon) {
        goto unexpected_symbol;
    }
    L.next();
    PARSE(L, uint64, date.minute);
    if (L.peek().kind != tok::colon) {
        goto unexpected_symbol;
    }
    L.next();
    PARSE(L, uint64, date.second);

    if (!(L.peek().kind == tok::end || L.peek().kind == tok::dot)) {
        goto unexpected_symbol;
    }

validate_and_return:
    if (!date.validate()) {
        auto t = L.peek();
        return util::unexpected(parse_error{
            L.string(), fmt::format("date {} is out of bounds", t.spelling),
            t});
    }
    return date;

unexpected_symbol:
    auto t = L.peek();
    return util::unexpected(parse_error{
        L.string(), fmt::format("unexpected symbol '{}'", t.spelling), t});
}

} // namespace uenv
