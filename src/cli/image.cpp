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
#include "help.h"
#include "image.h"
#include "inspect.h"
#include "ls.h"

namespace uenv {

std::string image_footer();

void image_args::add_cli(CLI::App& cli,
                         [[maybe_unused]] global_settings& settings) {
    auto* image_cli =
        cli.add_subcommand("image", "manage and query uenv images");

    // add the `uenv image ls` command
    ls_args.add_cli(*image_cli, settings);

    // add the `uenv image copy` command
    copy_args.add_cli(*image_cli, settings);

    // add the `uenv image add` command
    add_args.add_cli(*image_cli, settings);

    // add the `uenv image remove` command
    remove_args.add_cli(*image_cli, settings);

    // add the `uenv image inspect` command
    inspect_args.add_cli(*image_cli, settings);

    // add the `uenv image find` command
    find_args.add_cli(*image_cli, settings);

    // add the `uenv image pull` command
    pull_args.add_cli(*image_cli, settings);

    image_cli->footer(image_footer);
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
