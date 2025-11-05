// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <cstdint>

#include <CLI/CLI.hpp>

#include <uenv/settings.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

enum class cli_mode : std::uint32_t {
    unset,
    image_add,
    image_copy,
    image_delete,
    image_find,
    image_inspect,
    image_ls,
    image_pull,
    image_push,
    image_rm,
    repo_create,
    repo_migrate,
    repo_status,
    repo_update,
    run,
    start,
    status,
    build,
    completion
};

struct global_settings {
    global_settings();

    // the environment variables that were set when the application is started.
    const envvars::state calling_environment;

    // the verbosity level: used to set spdlog level
    int verbose = 0;

    // the command mode
    using enum cli_mode;
    cli_mode mode = unset;

    // configuration options: merged from config file, CLI options and defaults
    configuration config;
};

} // namespace uenv

#include <fmt/core.h>

template <> class fmt::formatter<uenv::cli_mode> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::cli_mode mode, FmtContext& ctx) const {
        using enum uenv::cli_mode;
        switch (mode) {
        case unset:
            return format_to(ctx.out(), "unset");
        case start:
            return format_to(ctx.out(), "start");
        case run:
            return format_to(ctx.out(), "run");
        case image_ls:
            return format_to(ctx.out(), "image-ls");
        case image_add:
            return format_to(ctx.out(), "image-add");
        case image_copy:
            return format_to(ctx.out(), "image-copy");
        case image_delete:
            return format_to(ctx.out(), "image-delete");
        case image_rm:
            return format_to(ctx.out(), "image-rm");
        case image_find:
            return format_to(ctx.out(), "image-find");
        case image_pull:
            return format_to(ctx.out(), "image-pull");
        case image_push:
            return format_to(ctx.out(), "image-push");
        case image_inspect:
            return format_to(ctx.out(), "image-inspect");
        case repo_create:
            return format_to(ctx.out(), "repo-create");
        case repo_migrate:
            return format_to(ctx.out(), "repo-migrate");
        case repo_status:
            return format_to(ctx.out(), "repo-status");
        case repo_update:
            return format_to(ctx.out(), "repo-update");
        case status:
            return format_to(ctx.out(), "status");
        case build:
            return format_to(ctx.out(), "build");
        case completion:
            return format_to(ctx.out(), "completion");
        }
        return format_to(ctx.out(), "unknown");
    }
};

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
