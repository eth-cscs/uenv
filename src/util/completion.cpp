#include <CLI/CLI.hpp>
#include <algorithm>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <ranges>

#include <util/completion.h>

namespace util {

namespace completion {

struct completion_item {
    std::vector<std::string> command, completions;
};

typedef std::vector<completion_item> completion_list;

// Construct command: list of subcommands extracted from CLI11
std::vector<std::string> get_command(CLI::App* cli) {
    if (cli == nullptr) {
        return std::vector<std::string>();
    }

    auto command = get_command(cli->get_parent());
    command.push_back(cli->get_name());
    return command;
}

// Recursively traverse tree of subcommands created by CLI11
// Creates list of completions for every subcommand
void traverse_subcommand_tree(completion_list &cl, CLI::App* cli) {
    if (cli == nullptr)
        return;

    const auto subcommands = cli->get_subcommands({});
    const auto options_non_positional = cli->get_options(
        [](CLI::Option* option) { return option->nonpositional(); });

    auto get_option_name = [](CLI::Option* option) {
        return option->get_name();
    };
    auto get_subcommand_name = [](CLI::App* subcommand) {
        return subcommand->get_name();
    };

    auto tmp1 = std::views::transform(options_non_positional, get_option_name);
    auto tmp2 = std::views::transform(subcommands, get_subcommand_name);
    std::vector<std::string> completions;
    std::ranges::copy(tmp1, std::back_inserter(completions));
    std::ranges::copy(tmp2, std::back_inserter(completions));

    std::vector<std::string> command;
    std::ranges::copy(get_command(cli), std::back_inserter(command));

    cl.push_back({command, completions});

    for (auto subcommand : subcommands)
        traverse_subcommand_tree(cl, subcommand);
}

completion_list create_completion_list(CLI::App* cli) {
    completion_list cl;
    traverse_subcommand_tree(cl, cli);
    return cl;
}

// Starting point function that generates bash completion script
// First creates a list of subcommands and corresponding completions
// Then generates bash completion script from this list
std::string bash_completion(CLI::App* cli) {
    completion_list cl = create_completion_list(cli);

    auto gen_bash_function = [](completion_item item) {
        return fmt::format(R"(_{}()
{{
    UENV_OPTS="{}"
}}

)",
                           fmt::join(item.command, "_"),
                           fmt::join(item.completions, " "));
    };
    auto prefix_functions =
        std::views::transform(cl, gen_bash_function);

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

    COMPREPLY=($(compgen -W "${UENV_OPTS}" -- "${cur}"))
}

complete -F _uenv_completions uenv
)";

    return fmt::format("{}{}", fmt::join(prefix_functions, ""), main_functions);
}

} // namespace completion
} // namespace util
