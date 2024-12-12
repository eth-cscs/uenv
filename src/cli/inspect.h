#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <uenv/env.h>

#include "uenv.h"

namespace uenv {

struct image_inspect_args {
    std::string label;
    // clang-format off
    std::string format =
        "name:    {name}\n"
        "version: {version}\n"
        "tag:     {tag}\n"
        "system:  {system}\n"
        "uarch:   {uarch}\n"
        "id:      {id}\n"
        "sha:     {sha256}\n"
        "date:    {date}";
    // clang-format on
    void add_cli(CLI::App&, global_settings& settings);
};

int image_inspect(const image_inspect_args& args,
                  const global_settings& settings);

} // namespace uenv

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
        return fmt::format_to(ctx.out(), "{{uenv: '{}'}}", opts.label);
    }
};
