#pragma once

#include <memory>
#include <string>

#include <fmt/core.h>

namespace uenv {

enum class tok {
    slash, // forward slash /
    comma, // comma ','
    colon, // colon ':'
    name,  // string, e.g. prgenv-gnu
    eof,   // end of file/input
    error  // special error state marker
};

// std::ostream& operator<<(std::ostream&, const tok&);

struct token {
    unsigned loc;
    tok kind;
    std::string spelling;
};

// std::ostream &operator<<(std::ostream &, const token &);

// Forward declare pimpled implementation.
class lexer_impl;

class lexer {
  public:
    lexer(const char *begin);

    token next();
    token peek(unsigned n = 0);

    ~lexer();

  private:
    std::unique_ptr<lexer_impl> impl_;
};

} // namespace uenv

template <> class fmt::formatter<uenv::tok> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext> constexpr auto format(uenv::tok const &t, FmtContext &ctx) const {
        switch (t) {
        case uenv::tok::colon:
            return fmt::format_to(ctx.out(), "colon");
        case uenv::tok::comma:
            return fmt::format_to(ctx.out(), "comma");
        case uenv::tok::slash:
            return fmt::format_to(ctx.out(), "slash");
        case uenv::tok::name:
            return fmt::format_to(ctx.out(), "name");
        case uenv::tok::eof:
            return fmt::format_to(ctx.out(), "eof");
        case uenv::tok::error:
            return fmt::format_to(ctx.out(), "error");
        }
        return fmt::format_to(ctx.out(), "?");
    }
};

template <> class fmt::formatter<uenv::token> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext> constexpr auto format(uenv::token const &t, FmtContext &ctx) const {
        return fmt::format_to(ctx.out(), "token({}, {}, '{}')", t.loc, t.kind, t.spelling);
    }
};
