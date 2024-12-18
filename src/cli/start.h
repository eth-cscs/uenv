// vim: ts=4 sts=4 sw=4 et

#include <optional>
#include <string>

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <uenv/env.h>

#include "uenv.h"

namespace uenv {

void start_help();

struct start_args {
    std::string uenv_description;
    bool ignore_tty = false;
    std::optional<std::string> view_description;
    void add_cli(CLI::App&, global_settings& settings);
};

int start(const start_args& args, const global_settings& settings);

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
        auto tmp = fmt::format_to(
            ctx.out(), "{{uenv: '{}', view: ", opts.uenv_description);
        if (!opts.view_description) {
            return fmt::format_to(tmp, "''}}", opts.uenv_description);
        }
        return fmt::format_to(tmp, "'{}'}}", *opts.view_description);
    }
};
