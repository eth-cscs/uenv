#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <optional>
#include <string>

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <uenv/env.h>

#include "uenv.h"

namespace uenv {

struct image_copy_args {
    std::string src_uenv_description;
    std::string dst_uenv_description;
    std::optional<std::string> token;
    std::optional<std::string> username;
    bool force = false;
    void add_cli(CLI::App&, global_settings& settings);
};

int image_copy(const image_copy_args& args, const global_settings& settings);

} // namespace uenv

template <> class fmt::formatter<uenv::image_copy_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::image_copy_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "(image pull {} {} .token={})",
                              opts.src_uenv_description,
                              opts.dst_uenv_description, opts.token);
    }
};
