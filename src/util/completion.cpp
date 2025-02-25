#include <CLI/CLI.hpp>
#include <algorithm>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <range/v3/all.hpp>
#include <ranges>

#include <util/completion.h>

namespace util {

namespace completion {

struct completion_item {
    std::vector<std::string> command, completions;
};

class completion_list {
  public:
    completion_list(CLI::App* cli);
    std::string bash_completion();

  private:
    std::vector<completion_item> completion_items;
    void traverse_subcommand_tree(CLI::App* cli);
};

// Construct command: list of subcommands extracted from CLI11
std::vector<std::string> get_command(CLI::App* cli) {
    if (cli == nullptr) {
        return std::vector<std::string>();
    }

    auto command = get_command(cli->get_parent());
    command.push_back(cli->get_name());
    return command;
}

completion_list::completion_list(CLI::App* cli) {
    traverse_subcommand_tree(cli);
}

// Recursively traverse tree of subcommands created by CLI11
// Creates list of completions for every subcommand
void completion_list::traverse_subcommand_tree(CLI::App* cli) {
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
    auto completions = ranges::view::concat(tmp1, tmp2);

    std::vector<std::string> command_vec;
    std::ranges::copy(get_command(cli), std::back_inserter(command_vec));
    std::vector<std::string> completions_vec;
    std::ranges::copy(completions, std::back_inserter(completions_vec));

    completion_items.push_back({command_vec, completions_vec});

    for (auto subcommand : subcommands)
        traverse_subcommand_tree(subcommand);
}

// Generates bash completion script from the list of subcommands and
// corresponding completions
std::string completion_list::bash_completion() {
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
        std::views::transform(completion_items, gen_bash_function);

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

// Starting point function that generates bash completion script
std::string create_completion(CLI::App* cli) {
    return fmt::format("{}", completion_list(cli).bash_completion());
}

} // namespace completion
} // namespace util
