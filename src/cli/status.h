#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <uenv/env.h>

#include "uenv.h"

namespace uenv {

struct status_args {
    void add_cli(CLI::App&, global_settings& settings);
};

int status(const status_args& args, const global_settings& settings);

} // namespace uenv

template <> class fmt::formatter<uenv::status_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format([[maybe_unused]] uenv::status_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "");
    }
};
