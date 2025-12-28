#pragma once

#include <string>

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include "uenv.h"

namespace uenv {

void image_add_help();
void image_rm_help();

struct image_add_args {
    std::string label;
    std::string source;
    bool move = false;
    void add_cli(CLI::App&, global_settings& settings);
};

struct image_rm_args {
    std::string label;
    void add_cli(CLI::App&, global_settings& settings);
};

int image_add(const image_add_args& args, const global_settings& settings);
int image_rm(const image_rm_args& args, const global_settings& settings);

} // namespace uenv

template <> class fmt::formatter<uenv::image_add_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::image_add_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{{source: '{}', label: '{}'}}",
                              opts.source, opts.label);
    }
};

#include <fmt/core.h>

template <> class fmt::formatter<uenv::image_rm_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::image_rm_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{{uenv: '{}'}}", opts.label);
    }
};
