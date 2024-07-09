// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include "uenv.h"

namespace uenv {

void start_help();

struct start_options {
    std::string image_str;
    std::string view_str;
    void add_cli(CLI::App &, global_settings &settings);
};

void start(const start_options &options, const global_settings &settings);

} // namespace uenv

template <> class fmt::formatter<uenv::start_options> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext> constexpr auto format(uenv::start_options const &opts, FmtContext &ctx) const {
        return fmt::format_to(ctx.out(), "start_options(image {}, view {})", opts.image_str, opts.view_str);
    }
};
