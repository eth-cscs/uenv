// vim: ts=4 sts=4 sw=4 et

#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "completion.h"
#include "completion_scripts.h"
#include "help.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

namespace {

struct completion_args {
    std::string shell_description;
};

std::string format_as(const completion_args& args) {
    return fmt::format("{{shell: '{}'}}", args.shell_description);
}

// print the completion script for a shell. The script is fixed: it calls
// `uenv __complete` to complete each command line (see complete.h).
int completion(const completion_args& args) {
    spdlog::info("completion with options {}", args);

    if (args.shell_description == "bash") {
        fmt::print("{}", completion_scripts::bash);
        return 0;
    }
    if (args.shell_description == "zsh") {
        fmt::print("{}", completion_scripts::zsh);
        return 0;
    }

    term::error("unknown shell {}, expected one of bash, zsh",
                args.shell_description);
    return 1;
}

std::string completion_footer() {
    using enum help::block::admonition;
    using help::block;
    using help::linebreak;
    std::vector<help::item> items{
        // clang-format off
        block{none, "Print the script that completes uenv command lines in a shell."},
        block{none, "The script is installed with uenv, so this is only needed if the"},
        block{none, "installed script is not loaded."},
        linebreak{},
        block{xmpl, "enable completion in the current bash shell"},
        block{code,   "source <(uenv completion bash)"},
        linebreak{},
        block{xmpl, "enable completion in the current zsh shell"},
        block{code,   "autoload -U compinit && compinit"},
        block{code,   "source <(uenv completion zsh)"},
        // clang-format on
    };
    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace

argparse::command
completion_command([[maybe_unused]] const global_settings& settings) {
    argparse::command_builder<completion_args> completion_cli(
        "completion", "generate completion script for a chosen shell");
    completion_cli
        .add_positional("shell", &completion_args::shell_description,
                        "shell for which to generate completion script (bash "
                        "or zsh)")
        .required()
        .complete(argparse::completion::custom("shell"));
    completion_cli.action(completion);
    completion_cli.footer(completion_footer);

    return std::move(completion_cli).build();
}

} // namespace uenv
