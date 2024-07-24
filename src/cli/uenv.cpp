// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/color.h>
#include <fmt/core.h>

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

    uenv::start_args start;
    uenv::run_args run;

    start.add_cli(cli, settings);
    run.add_cli(cli, settings);

    CLI11_PARSE(cli, argc, argv);

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
