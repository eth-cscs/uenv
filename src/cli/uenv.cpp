// vim: ts=4 sts=4 sw=4 et
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/config.h>
#include <uenv/log.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/settings.h>
#include <util/color.h>
#include <util/curl.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/lustre.h>

#include "add_remove.h"
#include "build.h"
#include "completion.h"
#include "config.h"
#include "delete.h"
#include "help.h"
#include "image.h"
#include "inspect.h"
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
    std::optional<std::string> cli_repo{};
    std::optional<std::vector<uenv::repo_label>> cli_repo_labels{};

    CLI::App cli(fmt::format("uenv {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", settings.verbose, "enable verbose output");
    cli.add_flag_callback(
        "--no-color", [&cli_config]() -> void { cli_config.color = false; },
        "disable color output");
    cli.add_flag_callback(
        "--color", [&cli_config]() -> void { cli_config.color = true; },
        "enable color output");
    cli.add_flag("--version", print_version, "print version");
    cli.add_option("--repo", cli_repo, "the uenv repository description");
    cli.add_option("--system", cli_config.system_name, "the system name");

    cli.footer(help_footer);

    uenv::start_args start;
    uenv::run_args run;
    uenv::image_args image;
    uenv::repo_args repo;
    uenv::status_args stat;
    uenv::build_args build;
    uenv::completion_args completion(&cli);
    uenv::configure_args configure;

    start.add_cli(cli, settings);
    run.add_cli(cli, settings);
    image.add_cli(cli, settings);
    // add the inspect command so that it can be invoked two ways
    //   uenv image inspect ...
    //   uenv inspect ...
    image.inspect_args.add_cli(cli, settings);
    repo.add_cli(cli, settings);
    stat.add_cli(cli, settings);
    build.add_cli(cli, settings);
    completion.add_cli(cli, settings);
    configure.add_cli(cli, settings);

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
    uenv::init_log(console_log_level);

    if (auto bin = util::exe_path()) {
        spdlog::info("using uenv {}", bin->string());
    }

    // print the version and exit if the --version flag was passed
    if (print_version) {
        term::msg("{}", UENV_VERSION);
        return 0;
    }

    // parse the repo flag if it was passed
    if (cli_repo) {
        if (const auto result = uenv::parse_repo_list(cli_repo.value())) {
            spdlog::info("selected repositories: {}",
                         fmt::join(result.value(), ", "));
            cli_repo_labels = result.value();
        } else {
            term::error("invalid --repo argument: {}",
                        result.error().description);
            return 1;
        }
    }

    // set the configuration according to defaults, cli options and config
    // files.
    if (auto full_config = uenv::load_config(cli_config, cli_repo_labels,
                                             settings.calling_environment)) {
        // print any warnings that were generated while loading configuration
        for (const auto& warning : full_config->warnings) {
            term::warn("{}", warning);
        }
        // generate_configuration applies checks to ensure that paths in the
        // config exist. If they don't it unsets them with warning messages.
        settings.config = uenv::generate_configuration(full_config.value());
    } else {
        term::error("{}", full_config.error());
        return 1;
    }

    if (settings.config.repos.empty()) {
        spdlog::warn("there is no valid repo - use the --repo flag or edit the "
                     "configuration to set a repo path");
    }

    //
    // perform actions based on the configuration
    //
    // toggle whether to use color output
    spdlog::info("color output is {}",
                 (settings.config.color ? "enabled" : "disabled"));
    color::set_color(settings.config.color);

    // locate the TLS trust store from the startup environment (honours
    // SSL_CERT_FILE / SSL_CERT_DIR) before any HTTPS request is made.
    // TODO: only perform this when interactions with OCI are required
    util::curl::configure_tls(settings.calling_environment);

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
    case settings.repo_migrate:
        return uenv::repo_migrate(repo.migrate_args, settings);
    case settings.repo_status:
        return uenv::repo_status(repo.status_args, settings);
    case settings.repo_update:
        return uenv::repo_update(repo.update_args, settings);
    case settings.status:
        return uenv::status(stat, settings);
    case settings.build:
        return uenv::build(build, settings);
    case settings.completion:
        return uenv::completion(completion);
    case settings.configure:
        return uenv::configure(configure, settings);
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
