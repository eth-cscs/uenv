#pragma once

#include <CLI/CLI.hpp>

#include "uenv.h"

namespace uenv {

struct configure_args {
    void add_cli(CLI::App&, global_settings& settings);
};

int configure(const configure_args& args, const global_settings& settings);

} // namespace uenv

#include <fmt/core.h>
template <> class fmt::formatter<uenv::configure_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format([[maybe_unused]] uenv::configure_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "configure_args[]");
    }
};
