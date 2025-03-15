// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <util/completion.h>
#include <util/expected.h>
#include <util/shell.h>

#include "completion.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

std::string completion_footer();

completion_args::completion_args(CLI::App* cli) : cli(cli) {
}

void completion_args::add_cli(CLI::App& cli, global_settings& settings) {
    auto* completion_cli = cli.add_subcommand(
        "completion", "generate completion script for a chosen shell");
    completion_cli
        ->add_option("shell", shell_description,
                     "shell for which to generate completion script")
        ->required();
    completion_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::completion; });
}

int completion(const completion_args& args) {
    spdlog::info("completion with options {}", args);

    if (args.shell_description == "bash") {
        fmt::print("{}", util::completion::bash_completion(args.cli, "uenv"));
        return 0;
    }

    term::error("unknown shell {}", args.shell_description);
    return 1;
}

} // namespace uenv
