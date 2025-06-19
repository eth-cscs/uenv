// vim: ts=4 sts=4 sw=4 et
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/log.h>

#include <uenv/config.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/settings.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/fs.h>

#include "add_remove.h"
#include "build.h"
#include "completion.h"
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

uenv::global_settings::global_settings() : calling_environment(environ) {
}

int main(int argc, char** argv) {
    uenv::config_base cli_config;
    uenv::global_settings settings;
    bool print_version = false;
    std::optional<std::string> config_file_path;

    CLI::App cli(fmt::format("uenv {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", settings.verbose, "enable verbose output");
    cli.add_flag_callback(
        "--no-color", [&cli_config]() -> void { cli_config.color = false; },
        "disable color output");
    cli.add_flag_callback(
        "--color", [&cli_config]() -> void { cli_config.color = true; },
        "enable color output");
    cli.add_flag("--version", print_version, "print version");
    cli.add_option("--repo", cli_config.repo, "the uenv repository");
    cli.add_option("--config", config_file_path, "path to configuration file");

    cli.footer(help_footer);

    uenv::start_args start;
    uenv::run_args run;
    uenv::image_args image;
    uenv::repo_args repo;
    uenv::status_args stat;
    uenv::build_args build;
    uenv::completion_args completion(&cli);

    start.add_cli(cli, settings);
    run.add_cli(cli, settings);
    image.add_cli(cli, settings);
    repo.add_cli(cli, settings);
    stat.add_cli(cli, settings);
    build.add_cli(cli, settings);
    completion.add_cli(cli, settings);

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

    // set the configuration according to defaults, cli options and config
    // files.
    uenv::config_base user_config;
    if (auto cfg = uenv::load_user_config(settings.calling_environment,
                                          config_file_path)) {
        user_config = *cfg;
    } else {
        term::error("error in configuration file:\n  {}", cfg.error());
        return 1;
    }
    spdlog::info("user    config: {}", user_config);
    const auto default_config =
        uenv::default_config(settings.calling_environment);
    spdlog::info("default config: {}", default_config);
    const auto full_config =
        uenv::merge(cli_config, uenv::merge(user_config, default_config));
    if (auto merged_config = uenv::generate_configuration(full_config)) {
        settings.config = merged_config.value();
    } else {
        term::error("an invalid configuration was provided: {}",
                    merged_config.error());
    }
    if (!settings.config.repo) {
        term::warn("there is no valid repo - use the --repo flag or edit the "
                   "configuration to set a repo path");
    }

    //
    // perform actions based on the configuration
    //
    // toggle whether to use color output
    spdlog::info("color output is {}",
                 (settings.config.color ? "enabled" : "disabled"));
    color::set_color(settings.config.color);

    // validate the user repository - attempt to create if it does not exist
    if (settings.config.repo) {
        using enum uenv::repo_state;
        const auto initial_state =
            uenv::validate_repository(settings.config.repo.value());
        switch (initial_state) {
        // repo exists and is read only
        case invalid:
            spdlog::warn("unable to create repository: {} is invalid",
                         settings.config.repo.value());
            break;
        // repo exists and is read only
        case readonly:
            spdlog::warn("the repo {} exists, but is read only, some "
                         "operations like image pull are disabled.",
                         settings.config.repo.value());
        // repo exists and is writable
        case readwrite:
            break;
        // repo does not exist - attempt to create
        // ignore any error - later attempts to use the repo can handle the
        // error
        default:
            spdlog::info("the repo {} does not exist - creating",
                         settings.config.repo.value());
            if (auto result =
                    uenv::create_repository(settings.config.repo.value());
                !result) {
                spdlog::warn("the repo {} was not created: {}",
                             settings.config.repo.value(), result.error());
            }
            break;
        }
    }

    // util::expected<repository, std::string>
    // create_repository(const fs::path& repo_path) {
    // using enum repo_state;

    // auto abs_repo_path = fs::absolute(repo_path);

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
    case settings.image_push:
        return uenv::image_push(image.push_args, settings);
    case settings.repo_create:
        return uenv::repo_create(repo.create_args, settings);
    case settings.repo_status:
        return uenv::repo_status(repo.status_args, settings);
    case settings.status:
        return uenv::status(stat, settings);
    case settings.build:
        return uenv::build(build, settings);
    case settings.completion:
        return uenv::completion(completion);
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
