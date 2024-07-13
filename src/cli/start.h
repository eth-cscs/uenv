// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <uenv/env.h>

#include "uenv.h"

namespace uenv {

void start_help();

struct start_args {
    std::string uenv_description;
    std::string view_description;
    void add_cli(CLI::App&, global_settings& settings);
};

void start(const start_args& args, const global_settings& settings);

} // namespace uenv

template <> class fmt::formatter<uenv::start_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::start_args const& opts, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "start_args(image {}, view {})",
                              opts.uenv_description, opts.view_description);
    }
};
