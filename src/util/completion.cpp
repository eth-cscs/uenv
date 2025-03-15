#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <ranges>

#include <util/completion.h>

namespace util {

namespace completion {

struct completion_item {
    std::vector<std::string> command;
    std::vector<std::string> completions;
};

typedef std::vector<completion_item> completion_list;

// Recursively traverse tree of subcommands created by CLI11
// Creates list of completions for every subcommand
void traverse_subcommand_tree(completion_list& cl, CLI::App* cli,
                              std::vector<std::string> command) {
    if (cli == nullptr) {
        return;
    }

    const auto subcommands = cli->get_subcommands({});

    std::vector<std::string> completions;
    for (const auto& cmd : subcommands) {
        completions.push_back(cmd->get_name());
    }
    for (const auto& option :
         cli->get_options([](auto* o) { return o->nonpositional(); })) {
        completions.push_back(option->get_name());
    }

    cl.push_back({command, completions});

    for (auto subcommand : subcommands) {
        command.push_back(subcommand->get_name());
        traverse_subcommand_tree(cl, subcommand, command);
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
}}

)",
                           fmt::join(item.command, "_"),
                           fmt::join(item.completions, " "));
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

    COMPREPLY=($(compgen -W "${UENV_OPTS}" -- "${cur}"))
}

complete -F _uenv_completions uenv
)";

    return fmt::format(
        "{}{}", fmt::join(std::views::transform(cl, gen_bash_function), ""),
        main_functions);
}

} // namespace completion
} // namespace util
