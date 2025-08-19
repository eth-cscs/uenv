#pragma once

#include <memory>
#include <string_view>

namespace lex {

enum class tok {
    at,         // at '@'
    slash,      // forward slash /
    integer,    // unsigned integer
    comma,      // comma ','
    colon,      // colon ':'
    symbol,     // string, e.g. prgenv
    dash,       // comma ','
    dot,        // comma ','
    whitespace, // spaces, tabs, etc. Contiguous white space characters are
                // joined together.
    bang,       // exclamation mark '!'
    hash,       // hash '#'
    equals,     // equal sign '='
    star,       // '*'
    plus,       // '+'
    percent,    // percentage symbol '%'
    end,        // end of input
    error,      // invalid input encountered in stream
};

// std::ostream& operator<<(std::ostream&, const tok&);

struct token {
    unsigned loc;
    tok kind;
    std::string_view spelling;
};
bool operator==(const token&, const token&);

// std::ostream &operator<<(std::ostream &, const token &);

// Forward declare pimpled implementation.
class lexer_impl;

class lexer {
  public:
    lexer(std::string_view input);

    token next();
    token peek(unsigned n = 0);

    // a convenience helper for checking the kind of the current token
    tok current_kind() const;

    // return a string view of the full input
    std::string string() const;

    // return true if the current token matches tok
    bool operator==(tok) const;

    ~lexer();

  private:
    std::unique_ptr<lexer_impl> impl_;
};

} // namespace lex

#include <fmt/core.h>

template <> class fmt::formatter<lex::tok> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(lex::tok const& t, FmtContext& ctx) const {
        switch (t) {
        case lex::tok::colon:
            return fmt::format_to(ctx.out(), "colon");
        case lex::tok::star:
            return fmt::format_to(ctx.out(), "star");
        case lex::tok::comma:
            return fmt::format_to(ctx.out(), "comma");
        case lex::tok::integer:
            return fmt::format_to(ctx.out(), "integer");
        case lex::tok::slash:
            return fmt::format_to(ctx.out(), "slash");
        case lex::tok::symbol:
            return fmt::format_to(ctx.out(), "symbol");
        case lex::tok::dot:
            return fmt::format_to(ctx.out(), "dot");
        case lex::tok::dash:
            return fmt::format_to(ctx.out(), "dash");
        case lex::tok::whitespace:
            return fmt::format_to(ctx.out(), "whitespace");
        case lex::tok::hash:
            return fmt::format_to(ctx.out(), "hash");
        case lex::tok::equals:
            return fmt::format_to(ctx.out(), "equals");
        case lex::tok::at:
            return fmt::format_to(ctx.out(), "at");
        case lex::tok::bang:
            return fmt::format_to(ctx.out(), "bang");
        case lex::tok::percent:
            return fmt::format_to(ctx.out(), "percent");
        case lex::tok::plus:
            return fmt::format_to(ctx.out(), "plus");
        case lex::tok::end:
            return fmt::format_to(ctx.out(), "end");
        case lex::tok::error:
            return fmt::format_to(ctx.out(), "error");
        }
        return fmt::format_to(ctx.out(), "?");
    }
};

template <> class fmt::formatter<lex::token> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(lex::token const& t, FmtContext& ctx) const {
        // return fmt::format_to(ctx.out(), "{loc: {}, kind: {}, spelling:
        // '{}')", t.loc, t.kind, t.spelling);
        return fmt::format_to(ctx.out(), "loc: {}, kind: {} '{}'", t.loc,
                              t.kind, t.spelling);
    }
};
