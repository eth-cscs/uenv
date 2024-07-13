#pragma once

#include <optional>
#include <string>
#include <variant>

#include <fmt/core.h>

#include <util/expected.h>

namespace uenv {

struct view_description {
    std::optional<std::string> uenv;
    std::string name;
};

struct uenv_label {
    std::string name;
    std::optional<std::string> version;
    std::optional<std::string> tag;
};

struct uenv_description {
    std::optional<uenv_label> label() const;
    std::optional<std::string> filename() const;
    uenv_description(uenv_label);
    uenv_description(std::string);
    uenv_description() = default;

  private:
    std::variant<uenv_label, std::string> value_;
};

struct parse_error {
    std::string msg;
    unsigned loc;
    parse_error(std::string msg, unsigned loc) : msg(std::move(msg)), loc(loc) {
    }
};

} // namespace uenv

template <> class fmt::formatter<uenv::view_description> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::view_description const& d,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "view_description({}, {})",
                              (d.uenv ? *d.uenv : "none"), d.name);
    }
};

template <> class fmt::formatter<uenv::uenv_description> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::uenv_description const& d,
                          FmtContext& ctx) const {
        if (const auto fname = d.filename())
            return fmt::format_to(ctx.out(), "uenv_description(file={})",
                                  *fname);
        // return fmt::format_to(ctx.out(), "uenv_description({})", d.label());
        return fmt::format_to(ctx.out(), "");
    }
};
