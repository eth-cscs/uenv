// vim: ts=4 sts=4 sw=4 et
#include <string>

#include <fmt/core.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <util/expected.h>
#include <util/shell.h>

#include "add_remove.h"
#include "image.h"
#include "ls.h"

namespace uenv {

void image_help() {
    fmt::println("image help");
}

void image_args::add_cli(CLI::App& cli,
                         [[maybe_unused]] global_settings& settings) {
    auto* image_cli =
        cli.add_subcommand("image", "manage and query uenv images");

    // add the `uenv image ls` command
    ls_args.add_cli(*image_cli, settings);

    // add the `uenv image add` command
    add_args.add_cli(*image_cli, settings);

    // add the `uenv image remove` command
    remove_args.add_cli(*image_cli, settings);
}

} // namespace uenv
