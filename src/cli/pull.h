#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <optional>
#include <string>

#include <CLI/CLI.hpp>

#include "uenv.h"

namespace uenv {

struct image_pull_args {
    std::string uenv_description;
    std::optional<std::string> token;
    std::optional<std::string> username;
    bool only_meta = false;
    bool force = false;
    bool build = false;
    void add_cli(CLI::App&, global_settings& settings);
};

int image_pull(const image_pull_args& args, const global_settings& settings);

} // namespace uenv

#include <fmt/core.h>

template <> class fmt::formatter<uenv::image_pull_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::image_pull_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(
            ctx.out(), "(image pull {} .only_meta={} .force={} .token={})",
            opts.uenv_description, opts.only_meta, opts.force, opts.token);
    }
};
