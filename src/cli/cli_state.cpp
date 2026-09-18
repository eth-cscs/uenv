// vim: ts=4 sts=4 sw=4 et
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <argparse/argparse.h>
#include <uenv/config.h>

#include "cli_state.h"
#include "help.h"

namespace uenv {

namespace {
std::string help_footer();
}

cli_state::cli_state(global_settings& settings)
    : completion(&root), root("uenv", fmt::format("uenv {}", UENV_VERSION)) {

    root.add_flag({'v', "verbose"}, settings.verbose, "enable verbose output");
    root.add_flag(
        "no-color", [this]() -> void { cli_config.color = false; },
        "disable color output");
    root.add_flag(
        "color", [this]() -> void { cli_config.color = true; },
        "enable color output");
    root.add_flag("version", print_version, "print version");
    root.add_option("repo", cli_repo, "the uenv repository description")
        .complete(argparse::completion::custom("repo"));
    root.add_option("system", cli_config.system_name, "the system name")
        .complete(argparse::completion::custom("system"));

    root.footer(help_footer);

    start.add_cli(root, settings);
    run.add_cli(root, settings);
    image.add_cli(root, settings);
    // add the inspect command so that it can be invoked two ways
    //   uenv image inspect ...
    //   uenv inspect ...
    image.inspect_args.add_cli(root, settings);
    repo.add_cli(root, settings);
    stat.add_cli(root, settings);
    build.add_cli(root, settings);
    completion.add_cli(root, settings);
    configure.add_cli(root, settings);
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
