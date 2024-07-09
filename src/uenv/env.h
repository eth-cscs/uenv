#pragma once

#include <optional>
#include <string>
#include <vector>

#include <fmt/core.h>

#include <util/expected.h>

namespace uenv {

struct view_description {
    std::optional<std::string> uenv;
    std::string name;
};

struct uenv_description {
    std::string name;
    std::optional<std::string> version;
    std::optional<std::string> tag;
};

struct parse_error {
    std::string msg;
    unsigned loc;
    parse_error(std::string msg, unsigned loc) : msg(std::move(msg)), loc(loc) {
    }
};

util::expected<std::vector<view_description>, parse_error> parse_view_args(const std::string &arg);

} // namespace uenv

template <> class fmt::formatter<uenv::view_description> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext> constexpr auto format(uenv::view_description const &d, FmtContext &ctx) const {
        return fmt::format_to(ctx.out(), "view_description({}, {})", (d.uenv ? *d.uenv : "none"), d.name);
    }
};
