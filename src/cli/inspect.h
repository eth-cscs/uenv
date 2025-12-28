#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <CLI/CLI.hpp>

#include "uenv.h"

namespace uenv {

struct image_inspect_args {
    std::string uenv;
    bool json = false;
    std::optional<std::string> format;
    void add_cli(CLI::App&, global_settings& settings);
};

int image_inspect(const image_inspect_args& args,
                  const global_settings& settings);

} // namespace uenv

#include <fmt/core.h>

template <> class fmt::formatter<uenv::image_inspect_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::image_inspect_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{{uenv: '{}'}}", opts.uenv);
    }
};
