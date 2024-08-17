// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/color.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <uenv/log.h>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>

#include "run.h"
#include "start.h"
#include "uenv.h"

void help() {
    fmt::println("usage main");
}

int main(int argc, char** argv) {
    uenv::global_settings settings;

    CLI::App cli("uenv");
    cli.add_flag("-v,--verbose", settings.verbose, "enable verbose output");
    cli.add_flag("--no-color", settings.no_color, "disable color output");
    cli.add_flag("--repo", settings.repo_, "the uenv repository");

    uenv::start_args start;
    uenv::run_args run;

    start.add_cli(cli, settings);
    run.add_cli(cli, settings);

    CLI11_PARSE(cli, argc, argv);

    // Warnings and errors are always logged. The verbosity level is increased
    // with repeated uses of --verbose.
    spdlog::level::level_enum console_log_level = spdlog::level::warn;
    if (settings.verbose == 1) {
        console_log_level = spdlog::level::info;
    } else if (settings.verbose == 2) {
        console_log_level = spdlog::level::debug;
    } else if (settings.verbose >= 3) {
        console_log_level = spdlog::level::trace;
    }
    uenv::init_log(console_log_level, spdlog::level::trace);

    spdlog::info("{}", settings);

    // if a repo was not provided as a flag, look at environment variables
    if (!settings.repo_) {
        if (const auto p = uenv::default_repo_path()) {
            settings.repo_ = *uenv::default_repo_path();
        } else {
            spdlog::warn("ignoring the default repo path: {}", p.error());
        }
    }

    // post-process settings after the CLI arguments have been parsed
    if (settings.repo_) {
        if (const auto rpath =
                uenv::validate_repo_path(*settings.repo_, false, false)) {
            settings.repo = *rpath;
        } else {
            spdlog::warn("ignoring repo path due to an error, {}",
                         rpath.error());
            settings.repo = std::nullopt;
            settings.repo_ = std::nullopt;
        }
    }

    if (settings.repo) {
        spdlog::info("repo is set {}", *settings.repo);
    }

    spdlog::info("{}", settings);

    switch (settings.mode) {
    case uenv::mode_start:
        return uenv::start(start, settings);
    case uenv::mode_run:
        return uenv::run(run, settings);
    case uenv::mode_none:
    default:
        help();
    }

    return 0;
}
