// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/color.h>
#include <fmt/core.h>

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

    if (const auto p = uenv::default_repo_path()) {
        settings.repo_ = *uenv::default_repo_path();
    } else {
        fmt::println("[error] unable to determine the default repo path {}",
                     p.error());
        return 1;
    }

    CLI::App cli("uenv");
    cli.add_flag("-v,--verbose", settings.verbose, "enable verbose output");
    cli.add_flag("--no-color", settings.no_color, "disable color output");
    cli.add_flag("--repo", settings.repo_, "the uenv repository");

    uenv::start_args start;
    uenv::run_args run;

    start.add_cli(cli, settings);
    run.add_cli(cli, settings);

    CLI11_PARSE(cli, argc, argv);

    // post-process settings after the CLI arguments have been parsed
    if (settings.repo_) {
        if (const auto rpath =
                uenv::validate_repo_path(*settings.repo_, false, false)) {
            settings.repo = *rpath;
        } else {
            fmt::println("[error] invalid repo path {}", rpath.error());
            return 1;
        }
    }

    if (settings.repo) {
        fmt::println("repo is set {}", *settings.repo);
    }

    fmt::print(fmt::emphasis::bold | fg(fmt::color::orange), "{}\n", settings);

    switch (settings.mode) {
    case uenv::mode_start:
        fmt::print(fmt::emphasis::bold | fg(fmt::color::light_cyan), "{}\n",
                   start);
        return uenv::start(start, settings);
    case uenv::mode_run:
        fmt::print(fmt::emphasis::bold | fg(fmt::color::light_cyan), "{}\n",
                   run);
        return uenv::run(run, settings);
    case uenv::mode_none:
    default:
        help();
    }

    return 0;
}
