// #include <optional>
#include <string>
#include <vector>

#include <uenv/env.h>
#include <uenv/lex.h>
#include <util/expected.h>

namespace uenv {

// some pre-processor gubbins that generates code to attempt parsing a value using a parse_x method.
// unwraps and forwards the error if there was an error.
// much ergonomics!
#define PARSE(L, TYPE, X)                                                                                              \
    {                                                                                                                  \
        if (auto rval__ = parse_##TYPE(L))                                                                             \
            X = *rval__;                                                                                               \
        else                                                                                                           \
            return util::unexpected(std::move(rval__.error()));                                                        \
    }

util::expected<std::string, parse_error> parse_name(lexer &L) {
    auto n = L.next();
    if (n.kind != tok::name) {
        return util::unexpected(parse_error{fmt::format("invalid symbol {}", n.spelling), n.loc});
    }
    return n.spelling;
}

util::expected<view_description, parse_error> parse_view(lexer &L) {
    // there are two valid inputs to parse
    // name1            ->  uenv=none,  name=name1
    // name1:name2      ->  uenv=name1, name=name2
    std::string name1;
    PARSE(L, name, name1);
    if (L.peek().kind == tok::colon) {
        // flush the :
        L.next();
        std::string name2;
        PARSE(L, name, name2);
        return view_description{name1, name2};
    }

    return view_description{{}, name1};
}

// generate a list of view descriptions from a CLI argument, e.g. prgenv-gnu:spack,modules
util::expected<std::vector<view_description>, parse_error> parse_view_args(const std::string &arg) {
    auto L = lexer(arg.c_str());
    std::vector<view_description> views;

    while (true) {
        view_description v;
        PARSE(L, view, v);
        views.push_back(std::move(v));

        if (L.peek().kind != tok::comma) {
            break;
        }
        // eat the comma
        L.next();
    }
    // if parsing finished and the string has not been consumed, and invalide token was encountered
    if (L.peek().kind != tok::eof) {
        auto s = L.peek();
        return util::unexpected(parse_error{fmt::format("unexpected symbol {}", s.spelling), s.loc});
    }

    return views;
}

util::expected<std::string, parse_error> parse_path(lexer &L) {
    auto cont = [](tok t) -> bool { return t == tok::slash || t == tok::name; };
    std::string path;
    while (cont(L.peek().kind)) {
        path += L.next().spelling;
    }

    return path;
}

// generate a list of uenv
/*
util::expected<std::vector<uenv_description>, parse_error> parse_uenv_args(const std::string &arg) {
auto L = lexer(arg.c_str());
std::vector<uenv_description> uenvs;

while (true) {
    view_description v;
    PARSE(L, view, v);
    views.push_back(std::move(v));

    if (L.peek().kind != tok::comma) {
        break;
    }
    // eat the comma
    L.next();
}
// if parsing finished and the string has not been consumed, and invalide token was encountered
if (L.peek().kind != tok::eof) {
    auto s = L.peek();
    return util::unexpected(parse_error{fmt::format("unexpected symbol {}", s.spelling), s.loc});
}

return views;
}
*/

} // namespace uenv
