// vim: ts=4 sts=4 sw=4 et

#include <list>
#include <ranges>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <util/expected.h>
#include <util/shell.h>

#include "completion.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

// forward declare the implementation
namespace impl {
std::string bash_completion(const argparse::command* cli,
                            const std::string& name);
}

std::string completion_footer();

completion_args::completion_args(const argparse::command* root) : root(root) {
}

argparse::command completion_args::cli(global_settings& settings) {
    argparse::command completion_cli(
        "completion", "generate completion script for a chosen shell");
    completion_cli
        .add_positional("shell", shell_description,
                        "shell for which to generate completion script")
        .required()
        .complete(argparse::completion::custom("shell"));
    completion_cli.on_selected(
        [&settings]() { settings.mode = uenv::cli_mode::completion; });

    return completion_cli;
}

int completion(const completion_args& args) {
    spdlog::info("completion with options {}", args);

    if (args.shell_description == "bash") {
        fmt::print("{}", impl::bash_completion(args.root, "uenv"));
        return 0;
    }

    term::error("unknown shell {}", args.shell_description);
    return 1;
}

// implementation
namespace impl {

struct completion_item {
    // call stack for command (stack base = uenv)
    std::list<std::string> call_stack;
    // list of subcommands for this command
    std::list<std::string> subcommands;
    // whether this command accepts a uenv label as positional argument
    bool uenv_label = false;
    // list of nonpositional options/flags for this command
    std::list<std::string> nonpositionals;
};
typedef std::list<completion_item> completion_list;

// Recursively traverse the tree of subcommands
// Creates list of completions for every subcommand
void traverse_subcommand_tree(completion_list& cl, const argparse::command* cli,
                              const std::list<std::string>& stack) {
    if (cli == nullptr) {
        return;
    }

    completion_item comp_item;
    comp_item.call_stack = stack;
    const auto subcommands = cli->subcommands();
    for (const auto* subcmd : subcommands) {
        comp_item.subcommands.push_back(subcmd->name());
    }
    for (const auto* posarg : cli->positionals()) {
        if (posarg->name() == "uenv") {
            comp_item.uenv_label = true;
            break;
        };
    }
    for (const auto* opt : cli->options()) {
        comp_item.nonpositionals.push_back(opt->display_name());
    }
    cl.push_back(std::move(comp_item));

    // recurse into subcommands
    for (auto* cmd : subcommands) {
        auto cmd_stack = stack;
        cmd_stack.push_back(cmd->name());
        traverse_subcommand_tree(cl, cmd, cmd_stack);
    }
}

completion_list create_completion_list(const argparse::command* cli,
                                       const std::string& name) {
    completion_list cl;
    traverse_subcommand_tree(cl, cli, {name});
    return cl;
}

// Starting point function that generates bash completion script
// First creates a list of subcommands and corresponding completions
// Then generates bash completion script from this list
std::string bash_completion(const argparse::command* cli,
                            const std::string& name) {
    completion_list cl = create_completion_list(cli, name);

    // generates bash function for each possible command/subcommand branch
    auto gen_bash_function = [](completion_item item) {
        return fmt::format(R"(_{}()
{{
    UENV_SUBCMDS="{}"
    UENV_NONPOSITIONALS="{}"
    UENV_ACCEPT_LABEL="{}"
}}

)",
                           fmt::join(item.call_stack, "_"),
                           fmt::join(item.subcommands, " "),
                           fmt::join(item.nonpositionals, " "),
                           item.uenv_label ? "true" : "false");
    };

#include "completion.bash.h"

    return fmt::format(
        "{}{}", fmt::join(std::views::transform(cl, gen_bash_function), ""),
        std::string_view(reinterpret_cast<const char*>(completion_bash_main),
                         completion_bash_main_len));
}
} // namespace impl

} // namespace uenv
