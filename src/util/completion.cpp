#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <util/completion.h>

namespace util {

namespace completion {

// void normalize_bash_function_name(std::string &str) {
//     std::replace(str.begin(), str.end(), '-', '_');
// }

std::string get_prefix(CLI::App* cli) {
    if (cli == nullptr)
        return "";

    CLI::App* parent = cli->get_parent();
    return get_prefix(parent) + "_" + cli->get_name();
}

void create_completion_rec(CLI::App* cli) {
    std::string func_name = get_prefix(cli);
    auto options = cli->get_options();
    auto subcommands = cli->get_subcommands({});

    std::vector<CLI::Option*> options_positional;
    options_positional.reserve(options.size());
    std::vector<CLI::Option*> options_non_positional;
    options_non_positional.reserve(options.size());
    auto is_positional = [](CLI::Option* option) {
        return option->get_positional();
    };
    auto is_non_positional = [](CLI::Option* option) {
        return option->nonpositional();
    };
    std::copy_if(options.begin(), options.end(),
                 std::back_inserter(options_positional), is_positional);
    std::copy_if(options.begin(), options.end(),
                 std::back_inserter(options_non_positional), is_non_positional);

    std::vector<std::string> options_non_positional_str;
    options_non_positional_str.reserve(options_non_positional.size());
    auto get_option_name = [](CLI::Option* option) {
        return option->get_name();
    };
    std::transform(options_non_positional.begin(), options_non_positional.end(),
                   std::back_inserter(options_non_positional_str),
                   get_option_name);

    std::vector<std::string> subcommands_str;
    subcommands_str.reserve(options.size());
    auto get_subcommand_name = [](CLI::App* subcommand) {
        return subcommand->get_name();
    };
    std::transform(subcommands.begin(), subcommands.end(),
                   std::back_inserter(subcommands_str), get_subcommand_name);

    std::vector<std::string> complete_str;
    complete_str.reserve(options_non_positional_str.size() +
                         subcommands_str.size());
    complete_str.insert(complete_str.end(), options_non_positional_str.begin(),
                        options_non_positional_str.end());
    complete_str.insert(complete_str.end(), subcommands_str.begin(),
                        subcommands_str.end());

    // TODO generic for all special args
    // TODO optimize the functions (reuse if possible)
    auto has_name = [](CLI::Option* option, std::string name) {
        return option->get_name() == name;
    };
    auto has_uenv = [has_name](CLI::Option* option) {
        return has_name(option, "uenv");
    };
    bool special_uenv = std::any_of(options_positional.begin(),
                                    options_positional.end(), has_uenv);

    // TODO convert to functional
    std::string completions = "";
    for (auto s : complete_str)
        completions += s + " ";
    // std::string completions = std::accumulate(complete_str.begin(),
    // complete_str.end(), "", [](std::string X, std::string Y) {return X + Y +
    // " ";});

    fmt::print(R"({func_name}()
{{
    UENV_OPTS="{completions}"
)",
               fmt::arg("func_name", func_name),
               fmt::arg("completions", completions));
    // TODO generic for all special args
    if (special_uenv) {
        std::string special_func_name = "_uenv_special_uenv";
        std::string special_opts_name = "UENV_SPECIAL_OPTS_UENV";

        fmt::print(R"(
    if typeset -f {special_func_name} >/dev/null
    then
        {special_func_name}
        UENV_OPTS+=" ${{{special_opts_name}}}"
    fi
)",
                   fmt::arg("special_func_name", special_func_name),
                   fmt::arg("special_opts_name", special_opts_name));
    }
    fmt::print("}}\n\n");

    for (auto* subcom : subcommands)
        create_completion_rec(subcom);
}

void create_completion(CLI::App* cli) {
    create_completion_rec(cli);

    fmt::print(R"(
_uenv_special_uenv()
{{
    UENV_SPECIAL_OPTS_UENV=$(uenv image ls --no-header | awk '{{print $1}}')
}}

_uenv_completions()
{{
    local cur prefix func_name UENV_OPTS

    local -a COMP_WORDS_NO_FLAGS
    local index=0
    while [[ "$index" -lt "$COMP_CWORD" ]]
    do
        if [[ "${{COMP_WORDS[$index]}}" == [a-z]* ]]
        then
            COMP_WORDS_NO_FLAGS+=("${{COMP_WORDS[$index]}}")
        fi
        let index++
    done
    COMP_WORDS_NO_FLAGS+=("${{COMP_WORDS[$COMP_CWORD]}}")
    local COMP_CWORD_NO_FLAGS=$((${{#COMP_WORDS_NO_FLAGS[@]}} - 1))

    cur="${{COMP_WORDS_NO_FLAGS[COMP_CWORD_NO_FLAGS]}}"
    prefix="_${{COMP_WORDS_NO_FLAGS[*]:0:COMP_CWORD_NO_FLAGS}}"
    func_name="${{prefix// /_}}"
    func_name="${{func_name//-/_}}"

    UENV_OPTS=""
    if typeset -f $func_name >/dev/null
    then
        $func_name
    fi

    COMPREPLY=($(compgen -W "${{UENV_OPTS}}" -- "${{cur}}"))
}}

complete -F _uenv_completions uenv
)");
}

} // namespace completion
} // namespace util
