#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <util/completion.h>

namespace util {

namespace completion {

// void normalize_bash_function_name(std::string &str) {
//     std::replace(str.begin(), str.end(), '-', '_');
// }

// std::vector<CLI::Option *> filter(std::vector<CLI::Option *> in, bool
// (*f)(CLI::Option *)) {
//     std::vector<const CLI::Option*> out;
//     out.reserve(in.size());
//     std::copy_if(in.begin(), in.end(),
//                  std::back_inserter(out), f);
//     return out;
// }

template <typename Ts, typename Td>
std::vector<Td> map(std::vector<Ts> in, Td (*f)(Ts)) {
    std::vector<std::string> out;
    out.reserve(in.size());
    std::transform(in.begin(), in.end(), std::back_inserter(out), f);
    return out;
}

template <typename Ts, typename Td>
Td reduce(std::vector<Ts> in, Td (*f)(Td, Ts), Td init) {
    return std::accumulate(in.begin(), in.end(), init, f);
}

template <typename T> T reduce(std::vector<T> in, T (*f)(T, T), T init) {
    return reduce<T, T>(in, f, init);
}

template <typename T, typename F> bool is_in(std::vector<T> vec, F&& f) {
    return std::any_of(vec.begin(), vec.end(), f);
}

std::string get_prefix(CLI::App* cli) {
    if (cli == nullptr)
        return "";

    CLI::App* parent = cli->get_parent();
    return get_prefix(parent) + "_" + cli->get_name();
}

std::string create_completion_rec(CLI::App* cli) {
    const auto subcommands = cli->get_subcommands({});
    const auto options_non_positional = cli->get_options(
        [](CLI::Option* option) {return option->nonpositional();});

    auto get_option_name = [](CLI::Option* option) {
        return option->get_name();
    };
    auto get_subcommand_name = [](CLI::App* subcommand) {
        return subcommand->get_name();
    };

    // TODO RJ do it smarter: ranges?
    std::vector<std::string> completions = map<CLI::Option*, std::string>(
        options_non_positional, get_option_name);
    std::vector<std::string> tmp = map<CLI::App*, std::string>(subcommands, get_subcommand_name);
    completions.insert(completions.end(), tmp.begin(), tmp.end());

    std::vector<std::string> func_command_str = {};
    func_command_str.push_back(fmt::format(R"({}()
{{
    UENV_OPTS="{}"
}}

)",
               get_prefix(cli), fmt::join(completions, " ")));

    std::vector<std::string> func_subcommands_str = map<CLI::App*, std::string>(
        subcommands, create_completion_rec);

    return fmt::format("{}{}", fmt::join(func_command_str, ""), fmt::join(func_subcommands_str, ""));
}

std::string create_completion(CLI::App* cli) {
    std::string prefix_functions = create_completion_rec(cli);

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
