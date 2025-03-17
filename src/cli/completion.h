// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <CLI/CLI.hpp>

#include "uenv.h"

namespace uenv {

void completion_help();

struct completion_args {
    std::string shell_description;
    CLI::App* cli;
    completion_args(CLI::App* cli);
    void add_cli(CLI::App&, global_settings& settings);
};

int completion(const completion_args& args);

} // namespace uenv

#include <fmt/core.h>

template <> class fmt::formatter<uenv::completion_args> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::completion_args const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{{uenv: '{}'}}",
                              opts.shell_description);
    }
};
