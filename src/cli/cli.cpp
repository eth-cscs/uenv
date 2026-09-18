// vim: ts=4 sts=4 sw=4 et
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <argparse/argparse.h>
#include <uenv/config.h>

#include "build.h"
#include "cli.h"
#include "completion.h"
#include "config.h"
#include "help.h"
#include "image.h"
#include "inspect.h"
#include "repo.h"
#include "run.h"
#include "start.h"
#include "status.h"
#include "terminal.h"

namespace uenv {

namespace {
std::string help_footer();
}

argparse::program<global_args> make_cli(const global_settings& settings) {
    argparse::command_builder<global_args> root(
        "uenv", fmt::format("uenv {}", UENV_VERSION));

    root.add_flag({'v', "verbose"}, &global_args::verbose,
                  "enable verbose output");
    root.add_flag("color", &global_args::color,
                  "enable or disable color output")
        .negation("no-color");
    root.add_flag("version", &global_args::version, "print version");
    root.add_option("repo", &global_args::repo,
                    "the uenv repository description")
        .complete(argparse::completion::custom("repo"));
    root.add_option("system", &global_args::system, "the system name")
        .complete(argparse::completion::custom("system"));

    // `uenv` on its own
    root.action([](const global_args&) {
        term::msg("uenv version {}", UENV_VERSION);
        term::msg("call 'uenv --help' for help");
        return 0;
    });
    root.footer(help_footer);

    root.add_subcommand(start_command(settings));
    root.add_subcommand(run_command(settings));
    root.add_subcommand(image_command(settings));
    // `uenv inspect` is an alias for `uenv image inspect`
    root.add_subcommand(image_inspect_command(settings));
    root.add_subcommand(repo_command(settings));
    root.add_subcommand(status_command(settings));
    root.add_subcommand(build_command(settings));
    root.add_subcommand(completion_command(settings));
    root.add_subcommand(config_command(settings));

    return argparse::program<global_args>(std::move(root));
}

namespace {

std::string help_footer() {
    using enum help::block::admonition;
    using help::lst;

    // clang-format off
    std::vector<help::item> items{
        help::block{none, "Use the --help flag in with sub-commands for more information."},
        help::linebreak{},
        help::block{xmpl, fmt::format("use the {} flag to generate more verbose output", lst{"-v"})},
        help::block{code,   "uenv -v  image ls    # info level logging"},
        help::block{code,   "uenv -vv image ls    # debug level logging"},
        help::linebreak{},
        help::block{xmpl, "get help with the run command"},
        help::block{code,   "uenv run --help"},
        help::linebreak{},
        help::block{xmpl, fmt::format("get help with the {} command", lst("image ls"))},
        help::block{code,   "uenv image ls --help"},
    };
    // clang-format on

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace

} // namespace uenv
