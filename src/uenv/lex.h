#pragma once

#include <memory>
#include <string>

#include <fmt/core.h>

namespace uenv {

enum class tok {
    at,         // at '@'
    slash,      // forward slash /
    comma,      // comma ','
    colon,      // colon ':'
    symbol,     // string, e.g. prgenv-gnu
    dash,       // comma ','
    dot,        // comma ','
    end,        // end of input
    whitespace, // sequence of spaces
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

    ~lexer();

  private:
    std::unique_ptr<lexer_impl> impl_;
};

} // namespace uenv

template <> class fmt::formatter<uenv::tok> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::tok const& t, FmtContext& ctx) const {
        switch (t) {
        case uenv::tok::colon:
            return fmt::format_to(ctx.out(), "colon");
        case uenv::tok::comma:
            return fmt::format_to(ctx.out(), "comma");
        case uenv::tok::slash:
            return fmt::format_to(ctx.out(), "slash");
        case uenv::tok::symbol:
            return fmt::format_to(ctx.out(), "symbol");
        case uenv::tok::dot:
            return fmt::format_to(ctx.out(), "dot");
        case uenv::tok::dash:
            return fmt::format_to(ctx.out(), "dash");
        case uenv::tok::whitespace:
            return fmt::format_to(ctx.out(), "whitespace");
        case uenv::tok::at:
            return fmt::format_to(ctx.out(), "at");
        case uenv::tok::end:
            return fmt::format_to(ctx.out(), "end");
        case uenv::tok::error:
            return fmt::format_to(ctx.out(), "error");
        }
        return fmt::format_to(ctx.out(), "?");
    }
};

template <> class fmt::formatter<uenv::token> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::token const& t, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{loc: {}, kind: {}, spelling: '{}')",
                              t.loc, t.kind, t.spelling);
    }
};
