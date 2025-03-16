#pragma once
// vim: ts=4 sts=4 sw=4 et

#include <optional>
#include <string>

#include <CLI/CLI.hpp>

#include "uenv.h"

namespace uenv {

struct image_delete_args {
    std::string uenv_description;
    std::optional<std::string> token;
    std::optional<std::string> username;
    void add_cli(CLI::App&, global_settings& settings);
};

int image_delete(const image_delete_args& args,
                 const global_settings& settings);

} // namespace uenv

#include <fmt/core.h>

template <> class fmt::formatter<uenv::image_delete_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::image_delete_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "(image delete {} .token={})",
                              opts.uenv_description, opts.token);
    }
};
