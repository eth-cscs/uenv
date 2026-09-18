// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <uenv/settings.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

struct global_settings {
    global_settings();

    // the environment variables that were set when the application is started.
    const envvars::state calling_environment;

    // the verbosity level: used to set spdlog level
    int verbose = 0;

    // configuration options: merged from config file, CLI options and defaults
    configuration config;
};

// common strings used in more than one location.
namespace messages {
[[maybe_unused]] static std::string no_repos() {
    return fmt::format("there are no repositories.\n"
                       "- create one with {}\n"
                       "- or use the {} flag to provide the location of "
                       "existing repositories\n"
                       "- or set the {} field in your config file",
                       color::yellow("uenv repo create"),
                       color::yellow("--repo"),
                       color::yellow("[[repositories]]"));
}

[[maybe_unused]] static std::string no_matches() {
    return fmt::format(
        "see available uenv using {}.\n"
        "use {} and {} to find and download images before using them.",
        color::yellow("uenv image ls"), color::yellow("uenv image find"),
        color::yellow("uenv image pull"));
}

} // namespace messages

} // namespace uenv

#include <fmt/core.h>

template <> class fmt::formatter<uenv::global_settings> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::global_settings const& opts,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "global_settings(verbose {})",
                              opts.verbose);
    }
};
