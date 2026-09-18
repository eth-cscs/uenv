// vim: ts=4 sts=4 sw=4 et
#include <unistd.h>

#include <fmt/core.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <argparse/argparse.h>

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
#include "cli_state.h"
#include "completion.h"
#include "config.h"
#include "delete.h"
#include "image.h"
#include "inspect.h"
#include "repo.h"
#include "run.h"
#include "start.h"
#include "status.h"
#include "terminal.h"
#include "uenv.h"

uenv::global_settings::global_settings() : calling_environment(environ) {
}

int main(int argc, char** argv) {
    uenv::global_settings settings;
    uenv::cli_state cli(settings);
    std::optional<std::vector<uenv::repo_label>> cli_repo_labels{};

    if (auto valid = cli.root.validate(); !valid) {
        term::error("internal error in the command line interface: {}",
                    valid.error());
        return 1;
    }

    // messages printed before the configuration is loaded (parse errors and
    // help) use color only if the terminal supports it
    color::set_color(color::default_color(settings.calling_environment));

    // the command to run, once the configuration has been loaded
    const argparse::command* selected_command = nullptr;
    {
        const auto parsed = argparse::parse(cli.root, argc, argv);
        const auto applied = argparse::apply(parsed);
        if (!applied) {
            const auto& e = applied.error();
            term::error("{}", e.message);
            term::hint("run '{} --help' for more information",
                       fmt::join(e.cmd->path(), " "));
            return 1;
        }
        if (applied->help) {
            fmt::print("{}", argparse::render_help(*applied->help));
            return 0;
        }
        selected_command = applied->selected;
    }

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
    if (cli.print_version) {
        term::msg("{}", UENV_VERSION);
        return 0;
    }

    // parse the repo flag if it was passed
    if (cli.cli_repo) {
        if (const auto result = uenv::parse_repo_list(*cli.cli_repo)) {
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
    if (auto full_config = uenv::load_config(cli.cli_config, cli_repo_labels,
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

    const auto& selected = *selected_command;
    if (selected.has_action()) {
        return selected.run();
    }
    // a command that only groups subcommands: `uenv` prints the version,
    // `uenv image` etc. print their help
    if (selected.parent() == nullptr) {
        term::msg("uenv version {}", UENV_VERSION);
        term::msg("call '{} --help' for help", argv[0]);
    } else {
        fmt::print("{}", argparse::render_help(selected));
    }
    return 0;
}
