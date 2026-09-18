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
#include "find.h"
#include "help.h"
#include "image.h"
#include "inspect.h"
#include "ls.h"
#include "pull.h"
#include "push.h"

namespace uenv {

namespace {
std::string image_footer();
}

argparse::command image_command(const global_settings& settings) {
    argparse::command_builder<> image_cli("image",
                                          "manage and query uenv images");

    image_cli.add_subcommand(image_ls_command(settings));
    image_cli.add_subcommand(image_add_command(settings));
    image_cli.add_subcommand(image_rm_command(settings));
    image_cli.add_subcommand(image_inspect_command(settings));
    image_cli.add_subcommand(image_find_command(settings));
    image_cli.add_subcommand(image_pull_command(settings));
    image_cli.add_subcommand(image_copy_command(settings));
    image_cli.add_subcommand(image_delete_command(settings));
    image_cli.add_subcommand(image_push_command(settings));

    image_cli.footer(image_footer);

    return std::move(image_cli).build();
}

namespace {

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

} // namespace

} // namespace uenv
