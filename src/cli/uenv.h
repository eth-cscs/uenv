// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <fmt/core.h>

#include <util/expected.h>

namespace uenv {

extern int mode;

constexpr int mode_none = 0;
constexpr int mode_start = 1;
constexpr int mode_run = 2;
// constexpr int mode_status = 1;
// constexpr int mode_image = 2;

struct global_settings {
    int verbose = 0;
    bool no_color = false;
    int mode = mode_none;

    // repo_ is the unverified string description of the repo path that is
    // either read from an environment variable or as a --repo CLI argument. the
    // value should be validated using uenv::validate_repo_path before use.
    std::optional<std::string> repo_;

    std::optional<std::filesystem::path> repo;
};

} // namespace uenv

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
        return fmt::format_to(ctx.out(), "global_settings(mode {}, verbose {})",
                              opts.mode, opts.verbose);
    }
};
