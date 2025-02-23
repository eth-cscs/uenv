#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <range/v3/all.hpp>
#include <ranges>

#include <util/completion.h>

namespace util {

namespace completion {

// Get list of subcommands separated by "_"
// Used as a function name in generated bash completion script
std::string get_prefix(CLI::App* cli) {
    if (cli == nullptr)
        return "";

    CLI::App* parent = cli->get_parent();
    return get_prefix(parent) + "_" + cli->get_name();
}

// Recursively traverse tree of subcommands created by CLI11
// Creates list of completions for every subcommand
std::string traverse_subcommand_tree(CLI::App* cli) {
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
    auto completions = ranges::view::concat(tmp1, tmp2);

    std::vector<std::string> func_command_str = {};
    func_command_str.push_back(fmt::format(R"({}()
{{
    UENV_OPTS="{}"
}}

)",
                                           get_prefix(cli),
                                           fmt::join(completions, " ")));

    auto func_subcommands_str =
        std::views::transform(subcommands, traverse_subcommand_tree);

    return fmt::format("{}{}", fmt::join(func_command_str, ""),
                       fmt::join(func_subcommands_str, ""));
}

// Starting point function that generates bash completion script
std::string create_completion(CLI::App* cli) {
    std::string prefix_functions = traverse_subcommand_tree(cli);

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

    return fmt::format("{}{}", prefix_functions, main_functions);
}

} // namespace completion
} // namespace util
