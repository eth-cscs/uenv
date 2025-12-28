#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <CLI/CLI.hpp>

#include "uenv.h"

namespace uenv {

struct image_ls_args {
    std::optional<std::string> uenv_description;
    std::optional<std::string> format;
    bool no_header = false;
    bool json = false;
    bool no_partials = false;
    void add_cli(CLI::App&, global_settings& settings);
};

int image_ls(const image_ls_args& args, const global_settings& settings);

} // namespace uenv

#include <fmt/core.h>

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
        return fmt::format_to(
            ctx.out(),
            "{{uenv: '{}', json: {}, no_partials: {}, no_header: {}}}",
            opts.uenv_description, opts.json, opts.no_partials, opts.no_header);
    }
};
