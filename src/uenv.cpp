// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include "start.h"
#include "uenv.h"

void help() {
    fmt::println("usage main");
}

int main(int argc, char **argv) {
    uenv::start_options start;
    std::string name;

    uenv::global_options settings;

    CLI::App cli("uenv");

    cli.add_flag("-v,--verbose", settings.verbose, "enable verbose output");
    auto *start_cli = cli.add_subcommand("start", "start a uenv session");

    start_cli->add_option("-v,--view", view, "");
    // const auto start_opts = start.cli_options(settings.mode, name);

    // to::run(cli_options, argc, argv + 1);
    CLI11_PARSE(cli, argc, argv);

    fmt::println("{}", settings);
    fmt::println("name: {}", name);

    switch (settings.mode) {
    case uenv::mode_start:
        uenv::start(start, settings);
        break;
    case uenv::mode_none:
    default:
        help();
    }

    return 0;
}
