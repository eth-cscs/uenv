// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <uenv/log.h>

#include <uenv/config.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/color.h>
#include <util/expected.h>
#include <util/fs.h>

#include "add_remove.h"
#include "build.h"
#include "delete.h"
#include "help.h"
#include "image.h"
#include "repo.h"
#include "run.h"
#include "start.h"
#include "status.h"
#include "terminal.h"
#include "uenv.h"

std::string help_footer();

//template <typename T, typename U, typename Func>
//std::vector<U> map(const std::vector<T>& input, Func func) {
//    std::vector<U> output;
//    output.reserve(input.size()); // Optimize allocation
//    std::transform(input.begin(), input.end(), std::back_inserter(output), func);
//    return output;
//}

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

    //auto options_str = map<CLI::Option, std::string>(options, [] (CLI::Option *option) -> std::string {return option->get_name();});
    std::vector<std::string> options_str;
    options_str.reserve(options.size());
    std::transform(options.begin(), options.end(), std::back_inserter(options_str), [] (auto *option) {return option->get_name();});
    std::vector<std::string> subcommands_str;
    subcommands_str.reserve(options.size());
    std::transform(subcommands.begin(), subcommands.end(), std::back_inserter(subcommands_str), [] (auto *subcommand) {return subcommand->get_name();});

    std::vector<std::string> complete_str;
    complete_str.reserve(options_str.size() + subcommands_str.size());
    complete_str.insert(complete_str.end(), options_str.begin(), options_str.end());
    complete_str.insert(complete_str.end(), subcommands_str.begin(), subcommands_str.end());

    std::string completions = "";
    for (auto s: complete_str)
        completions += s + " ";

    
    std::cout << func_name << "()" << std::endl;
    std::cout << "{" << std::endl;
    std::cout << "    UENV_OPTS=\"" << completions << "\"" << std::endl;
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
    std::cout << "_uenv_completions()" << std::endl;
    std::cout << "{" << std::endl;
    std::cout << "    local cur prefix func_name UENV_OPTS" << std::endl;
    std::cout << "    cur=\"${COMP_WORDS[COMP_CWORD]}\"" << std::endl;
    std::cout << "    prefix=\"_${COMP_WORDS[*]:0:COMP_CWORD}\"" << std::endl;
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

int main(int argc, char** argv) {
    uenv::global_settings settings;
    bool print_version = false;
    bool generate_completion = false;

    // enable/disable color depending on NOCOLOR env. var and tty terminal
    // status.
    color::default_color();

    CLI::App cli(fmt::format("uenv {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", settings.verbose, "enable verbose output");
    cli.add_flag_callback(
        "--no-color", []() -> void { color::set_color(false); },
        "disable color output");
    cli.add_flag_callback(
        "--color", []() -> void { color::set_color(true); },
        "enable color output");
    cli.add_flag("--repo", settings.repo_, "the uenv repository");
    cli.add_flag("--version", print_version, "print version");
    cli.add_flag("--generate-completion", generate_completion, "generate bash completion script");

    cli.footer(help_footer);

    uenv::start_args start;
    uenv::run_args run;
    uenv::image_args image;
    uenv::repo_args repo;
    uenv::status_args stat;
    uenv::build_args build;

    start.add_cli(cli, settings);
    run.add_cli(cli, settings);
    image.add_cli(cli, settings);
    repo.add_cli(cli, settings);
    stat.add_cli(cli, settings);
    build.add_cli(cli, settings);

    CLI11_PARSE(cli, argc, argv);

    // By default there is no logging to the console
    //   user-friendly logging of errors and warnings is handled using
    //   term::error and term::warn
    // The level of logging is increased by adding --verbose
    spdlog::level::level_enum console_log_level = spdlog::level::off;
    if (settings.verbose == 1) {
        console_log_level = spdlog::level::info;
    } else if (settings.verbose == 2) {
        console_log_level = spdlog::level::debug;
    } else if (settings.verbose >= 3) {
        console_log_level = spdlog::level::trace;
    }
    // note: syslog uses level::info to capture key events
    uenv::init_log(console_log_level, spdlog::level::info);

    if (auto bin = util::exe_path()) {
        spdlog::info("using uenv {}", bin->string());
    }
    if (auto oras = util::oras_path()) {
        spdlog::info("using oras {}", oras->string());
    }

    // print the version and exit if the --version flag was passed
    if (print_version) {
        term::msg("{}", UENV_VERSION);
        return 0;
    }

    // generate bash completion script and exit if the --generate-completion flag was passed
    if (generate_completion) {
        create_completion(&cli);
        return 0;
    }

    // if a repo was not provided as a flag, look at environment variables
    if (!settings.repo_) {
        if (const auto p = uenv::default_repo_path()) {
            settings.repo_ = *p;
        } else {
            spdlog::warn("ignoring the repo path: {}", p.error());
            term::warn("ignoring the repo path: {}", p.error());
        }
    }

    // post-process settings after the CLI arguments have been parsed
    if (settings.repo_) {
        if (const auto rpath =
                uenv::validate_repo_path(*settings.repo_, false, false)) {
            settings.repo = *rpath;
        } else {
            term::warn("ignoring repo path: {}", rpath.error());
            settings.repo = std::nullopt;
            settings.repo_ = std::nullopt;
        }
    }

    if (settings.repo) {
        spdlog::info("using repo {}", *settings.repo);
    }

    spdlog::info("{}", settings);

    switch (settings.mode) {
    case settings.start:
        return uenv::start(start, settings);
    case settings.run:
        return uenv::run(run, settings);
    case settings.image_ls:
        return uenv::image_ls(image.ls_args, settings);
    case settings.image_add:
        return uenv::image_add(image.add_args, settings);
    case settings.image_copy:
        return uenv::image_copy(image.copy_args, settings);
    case settings.image_delete:
        return uenv::image_delete(image.delete_args, settings);
    case settings.image_inspect:
        return uenv::image_inspect(image.inspect_args, settings);
    case settings.image_rm:
        return uenv::image_rm(image.remove_args, settings);
    case settings.image_find:
        return uenv::image_find(image.find_args, settings);
    case settings.image_pull:
        return uenv::image_pull(image.pull_args, settings);
    case settings.repo_create:
        return uenv::repo_create(repo.create_args, settings);
    case settings.repo_status:
        return uenv::repo_status(repo.status_args, settings);
    case settings.status:
        return uenv::status(stat, settings);
    case settings.build:
        return uenv::build(build, settings);
    case settings.unset:
        term::msg("uenv version {}", UENV_VERSION);
        term::msg("call '{} --help' for help", argv[0]);
        return 0;
    default:
        spdlog::warn("{}", (int)settings.mode);
        term::error("internal error, missing implementation for mode {}",
                    settings.mode);
        return 1;
    }

    return 0;
}

std::string help_footer() {
    using enum help::block::admonition;
    using help::lst;

    // clang-format off
    std::vector<help::item> items{
        help::block{none, "Use the --help flag in with sub-commands for more information."},
        help::linebreak{},
        help::block{xmpl, fmt::format("use the {} flag to generate more verbose output", lst{"-v"})},
        help::block{code,   "uenv -v  image ls    # info level logging"},
        help::block{code,   "uenv -vv image ls    # debug level logging"},
        help::linebreak{},
        help::block{xmpl, "get help with the run command"},
        help::block{code,   "uenv run --help"},
        help::linebreak{},
        help::block{xmpl, fmt::format("get help with the {} command", lst("image ls"))},
        help::block{code,   "uenv image ls --help"},
    };
    // clang-format on

    return fmt::format("{}", fmt::join(items, "\n"));
}
