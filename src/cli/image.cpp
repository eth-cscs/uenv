// vim: ts=4 sts=4 sw=4 et
#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <util/expected.h>
#include <util/shell.h>

#include "add_remove.h"
#include "copy.h"
#include "delete.h"
#include "help.h"
#include "image.h"
#include "inspect.h"
#include "ls.h"

namespace uenv {

std::string image_footer();

argparse::command image_args::cli(const global_settings& settings) {
    argparse::command image_cli("image", "manage and query uenv images");

    // add the `uenv image ls` command
    image_cli.add_subcommand(ls_args.cli(settings));

    // add the `uenv image add` command
    image_cli.add_subcommand(add_args.cli(settings));

    // add the `uenv image remove` command
    image_cli.add_subcommand(remove_args.cli(settings));

    // add the `uenv image inspect` command
    image_cli.add_subcommand(inspect_args.cli(settings));

    // add the `uenv image find` command
    image_cli.add_subcommand(find_args.cli(settings));

    // add the `uenv image pull` command
    image_cli.add_subcommand(pull_args.cli(settings));

    // add the `uenv image copy` command
    image_cli.add_subcommand(copy_args.cli(settings));

    // add the `uenv image delete` command
    image_cli.add_subcommand(delete_args.cli(settings));

    // add the `uenv image push` command
    image_cli.add_subcommand(push_args.cli(settings));

    image_cli.footer(image_footer);

    return image_cli;
}

std::string image_footer() {
    using enum help::block::admonition;
    using help::lst;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Manage and query uenv images." },
        help::linebreak{},
        help::block{none, fmt::format("For more information on how to use individual commands use the {} flag.", lst("--help")) },
        help::linebreak{},
        help::block{xmpl, fmt::format("get help on the {} command", lst("ls"))},
        help::block{code,   "uenv image ls --help"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
