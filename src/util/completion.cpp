#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <util/completion.h>

namespace util {

namespace completion {

//void normalize_bash_function_name(std::string &str) {
//    std::replace(str.begin(), str.end(), '-', '_');
//}

std::string get_prefix(CLI::App *cli) {
    if (cli == nullptr)
        return "";

    CLI::App *parent = cli->get_parent();
    return get_prefix(parent) + "_" + cli->get_name();
}

void create_completion_rec(CLI::App *cli) {
    std::string func_name = get_prefix(cli);
    auto options = cli->get_options();
    auto subcommands = cli->get_subcommands({});

    std::vector<CLI::Option*> options_positional;
    options_positional.reserve(options.size());
    std::vector<CLI::Option*> options_non_positional;
    options_non_positional.reserve(options.size());
    auto is_positional = [](CLI::Option* option) {return option->get_positional();};
    auto is_non_positional = [](CLI::Option* option) {return option->nonpositional();};
    std::copy_if(options.begin(), options.end(), std::back_inserter(options_positional), is_positional);
    std::copy_if(options.begin(), options.end(), std::back_inserter(options_non_positional), is_non_positional);

    std::vector<std::string> options_non_positional_str;
    options_non_positional_str.reserve(options_non_positional.size());
    auto get_option_name = [](CLI::Option* option) {return option->get_name();};
    std::transform(options_non_positional.begin(), options_non_positional.end(), std::back_inserter(options_non_positional_str), get_option_name);

    std::vector<std::string> subcommands_str;
    subcommands_str.reserve(options.size());
    auto get_subcommand_name = [](CLI::App* subcommand) {return subcommand->get_name();};
    std::transform(subcommands.begin(), subcommands.end(), std::back_inserter(subcommands_str), get_subcommand_name);

    std::vector<std::string> complete_str;
    complete_str.reserve(options_non_positional_str.size() + subcommands_str.size());
    complete_str.insert(complete_str.end(), options_non_positional_str.begin(), options_non_positional_str.end());
    complete_str.insert(complete_str.end(), subcommands_str.begin(), subcommands_str.end());

    //TODO generic for all special args
    //TODO optimize the functions (reuse if possible)
    auto has_name = [](CLI::Option* option, std::string name) {return option->get_name() == name;};
    auto has_uenv = [has_name](CLI::Option* option) {return has_name(option, "uenv");};
    bool special_uenv = std::any_of(options_positional.begin(), options_positional.end(), has_uenv);

    //TODO convert to functional
    std::string completions = "";
    for (auto s: complete_str)
        completions += s + " ";
    //std::string completions = std::accumulate(complete_str.begin(), complete_str.end(), "", [](std::string X, std::string Y) {return X + Y + " ";});


    std::cout << func_name << "()" << std::endl;
    std::cout << "{" << std::endl;
    std::cout << "    UENV_OPTS=\"" << completions << "\"" << std::endl;
    //TODO generic for all special args
    if (special_uenv) {
        std::string special_func_name = "_uenv_special_uenv";
        std::string special_opts_name = "UENV_SPECIAL_OPTS_UENV";

        std::cout << std::endl;
        std::cout << "    if typeset -f " << special_func_name << " >/dev/null" << std::endl;
        std::cout << "    then" << std::endl;
        std::cout << "        " << special_func_name << std::endl;
        std::cout << "        UENV_OPTS+=\" ${" << special_opts_name << "}\"" << std::endl;
        std::cout << "    fi" << std::endl;
    }
    std::cout << "}" << std::endl;
    std::cout << std::endl;

    //std::cout << "app: " << cli->get_name() << std::endl;
    //std::cout << "prefix: " << get_prefix(cli) << std::endl;
    ////for (auto s: complete_str)
    ////    std::cout << "p: " << get_prefix(cli) << ": " << s << std::endl;
    //std::cout << "p: " << get_prefix(cli) << ": " << completions << std::endl;
    //std::cout << "options " << cli->get_name() << ":" << std::endl;
    //for(auto *option : options)
    //    std::cout << "Option: " << option->get_name() << '\n';
    //std::cout << "subcommands " << cli->get_name() << ":" << std::endl;
    for(auto *subcom : subcommands) {
    //    std::cout << "Subcommand: " << subcom->get_name() << '\n';

        create_completion_rec(subcom);

    //    std::cout << "Subcommand: " << subcom->get_name() << " end" << '\n';
    }
}

void create_completion(CLI::App *cli) {
    create_completion_rec(cli);

    std::cout << std::endl;
    std::cout << "_uenv_special_uenv()" << std::endl;
    std::cout << "{" << std::endl;
    std::cout << "    UENV_SPECIAL_OPTS_UENV=$(uenv image ls --no-header | awk '{print $1}')" << std::endl;
    std::cout << "}" << std::endl;
    std::cout << std::endl;
    std::cout << "_uenv_completions()" << std::endl;
    std::cout << "{" << std::endl;
    std::cout << "    local cur prefix func_name UENV_OPTS" << std::endl;
    std::cout << std::endl;
    std::cout << "    local -a COMP_WORDS_NO_FLAGS" << std::endl;
    std::cout << "    local index=0" << std::endl;
    std::cout << "    while [[ \"$index\" -lt \"$COMP_CWORD\" ]]" << std::endl;
    std::cout << "    do" << std::endl;
    std::cout << "        if [[ \"${COMP_WORDS[$index]}\" == [a-z]* ]]" << std::endl;
    std::cout << "        then" << std::endl;
    std::cout << "            COMP_WORDS_NO_FLAGS+=(\"${COMP_WORDS[$index]}\")" << std::endl;
    std::cout << "        fi" << std::endl;
    std::cout << "        let index++" << std::endl;
    std::cout << "    done" << std::endl;
    std::cout << "    COMP_WORDS_NO_FLAGS+=(\"${COMP_WORDS[$COMP_CWORD]}\")" << std::endl;
    std::cout << "    local COMP_CWORD_NO_FLAGS=$((${#COMP_WORDS_NO_FLAGS[@]} - 1))" << std::endl;
    std::cout << std::endl;
    std::cout << "    cur=\"${COMP_WORDS_NO_FLAGS[COMP_CWORD_NO_FLAGS]}\"" << std::endl;
    std::cout << "    prefix=\"_${COMP_WORDS_NO_FLAGS[*]:0:COMP_CWORD_NO_FLAGS}\"" << std::endl;
    std::cout << "    func_name=\"${prefix// /_}\"" << std::endl;
    std::cout << "    func_name=\"${func_name//-/_}\"" << std::endl;
    std::cout <<  std::endl;
    std::cout << "    UENV_OPTS=\"\"" << std::endl;
    std::cout << "    if typeset -f $func_name >/dev/null" << std::endl;
    std::cout << "    then" << std::endl;
    std::cout << "        $func_name" << std::endl;
    std::cout << "    fi" << std::endl;
    std::cout <<  std::endl;
    std::cout << "    COMPREPLY=($(compgen -W \"${UENV_OPTS}\" -- \"${cur}\"))" << std::endl;
    std::cout << "}" << std::endl;
    std::cout << std::endl;
    std::cout << "complete -F _uenv_completions uenv" << std::endl;
}

} // namespace completion
} // namespace util
