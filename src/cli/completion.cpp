// vim: ts=4 sts=4 sw=4 et

#include <ranges>
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

#include "completion.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

// forward declare the implementation
namespace impl {
std::string bash_completion(CLI::App* cli, const std::string& name);
}

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
        fmt::print("{}", impl::bash_completion(args.cli, "uenv"));
        return 0;
    }

    term::error("unknown shell {}", args.shell_description);
    return 1;
}

// implementation
namespace impl {

struct completion_item {
    std::vector<std::string> command;
    std::vector<std::string> completions;
    bool file_completion = false;
};

typedef std::vector<completion_item> completion_list;

// Recursively traverse tree of subcommands created by CLI11
// Creates list of completions for every subcommand
void traverse_subcommand_tree(completion_list& cl, CLI::App* cli,
                              const std::vector<std::string>& stack) {
    if (cli == nullptr) {
        return;
    }

    const auto subcommands = cli->get_subcommands({});
    bool file_completion = false;

    std::vector<std::string> completions;
    for (const auto& cmd : subcommands) {
        completions.push_back(cmd->get_name());
    }
    for (const auto& option :
         cli->get_options([](auto* o) { return o->nonpositional(); })) {
        completions.push_back(option->get_name());
    }
    for (const auto& option :
         cli->get_options([](auto* o) { return o->get_positional(); })) {
        if (option->get_name() == "uenv") {
            file_completion = true;
        };
    }

    cl.push_back({stack, completions, file_completion});

    for (auto& cmd : subcommands) {
        auto cmd_stack = stack;
        cmd_stack.push_back(cmd->get_name());
        traverse_subcommand_tree(cl, cmd, cmd_stack);
    }
}

completion_list create_completion_list(CLI::App* cli, const std::string& name) {
    completion_list cl;
    traverse_subcommand_tree(cl, cli, {name});
    return cl;
}

// Starting point function that generates bash completion script
// First creates a list of subcommands and corresponding completions
// Then generates bash completion script from this list
std::string bash_completion(CLI::App* cli, const std::string& name) {
    completion_list cl = create_completion_list(cli, name);

    auto gen_bash_function = [](completion_item item) {
        return fmt::format(R"(_{}()
{{
    UENV_OPTS="{}"
    FILE_COMPLETION="{}"
}}

)",
                           fmt::join(item.command, "_"),
                           fmt::join(item.completions, " "),
                           item.file_completion ? "true" : "false");
    };

    std::string main_functions = R"(
_uenv_completions()
{
    local cur prefix func_name UENV_OPTS

    local -a COMP_WORDS_NO_FLAGS
    local index=0
    while [[ "$index" -lt "$COMP_CWORD" ]]
    do
        if [[ "${COMP_WORDS[$index]}" == [a-z]* ]]
        then
            COMP_WORDS_NO_FLAGS+=("${COMP_WORDS[$index]}")
        fi
        let index++
    done
    COMP_WORDS_NO_FLAGS+=("${COMP_WORDS[$COMP_CWORD]}")
    local COMP_CWORD_NO_FLAGS=$((${#COMP_WORDS_NO_FLAGS[@]} - 1))

    cur="${COMP_WORDS_NO_FLAGS[COMP_CWORD_NO_FLAGS]}"
    prefix="_${COMP_WORDS_NO_FLAGS[*]:0:COMP_CWORD_NO_FLAGS}"
    func_name="${prefix// /_}"
    func_name="${func_name//-/_}"

    UENV_OPTS=""
    if typeset -f $func_name >/dev/null
    then
        $func_name
    fi

    SUBCOMMAND_OPTS=$(compgen -W "${UENV_OPTS}" -- "${cur}")
    FILE_OPTS=""
    if [ "${FILE_COMPLETION}" = "true" ]; then
        FILE_OPTS=$(compgen -f -d -- "${cur}")
    fi

    COMPREPLY=(${SUBCOMMAND_OPTS} ${FILE_OPTS})
}

complete -F _uenv_completions uenv
)";

    return fmt::format(
        "{}{}", fmt::join(std::views::transform(cl, gen_bash_function), ""),
        main_functions);
}
} // namespace impl

} // namespace uenv
