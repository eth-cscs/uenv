// vim: ts=4 sts=4 sw=4 et

#include <vector>

#include <fmt/core.h>

#include <uenv/env.h>
#include <uenv/parse.h>
#include <util/expected.h>

#include "start.h"
#include "uenv.h"

namespace uenv {

void start_help() {
    fmt::println("start help");
}

void start_args::add_cli(CLI::App& cli, global_settings& settings) {
    auto* start_cli = cli.add_subcommand("start", "start a uenv session");
    start_cli->add_option("-v,--view", view_description,
                          "comma separated list of views to load");
    start_cli
        ->add_option("uenv", uenv_description,
                     "comma separated list of uenv to mount")
        ->required();
    start_cli->callback([&settings]() { settings.mode = uenv::mode_start; });
}

// stores the parsed and validated settings, based on the CLI arguments.
struct start_settings {
    const std::vector<uenv::view_description> views;
};

util::expected<start_settings, std::string>
parse_start_args(const start_args& args) {
    // step 1: parse the uenv information
    //  inputs: (uenv cli argument)
    //  - parse the CLI uenv description
    //  inputs: (global settings, parsed cli argument)
    auto uenvs = uenv::parse_uenv_args(args.uenv_description);
    //  - lookup either file name or database (the filename can be relative in
    //  uenv, but absolute in slurm)
    // TODO
    //  - generate uenv meta data
    // step 2: parse the views
    //  - parse the CLI view description
    //  - look up view descriptions in the uenv meta data
    auto views = uenv::parse_view_args(args.view_description);
    if (!views) {
        return util::unexpected(
            fmt::format("invalid view description: {}", views.error().msg));
    }

    return start_settings{*views};
}

void start(const start_args& args, const global_settings& globals) {
    fmt::println("running start with options {}", args);
    const auto settings = parse_start_args(args);
    for (auto& v : settings->views) {
        fmt::print("  with view: {}\n", v);
    }
}

} // namespace uenv
