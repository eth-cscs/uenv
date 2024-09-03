#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <uenv/env.h>

#include "uenv.h"

namespace uenv {

void image_ls_help();

struct image_ls_args {
    std::optional<std::string> uenv_description;
    bool no_header = false;
    void add_cli(CLI::App&, global_settings& settings);
};

int image_ls(const image_ls_args& args, const global_settings& settings);

} // namespace uenv

template <> class fmt::formatter<uenv::image_ls_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::image_ls_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{{uenv: '{}'}}",
                              opts.uenv_description);
    }
};
