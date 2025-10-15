// vim: ts=4 sts=4 sw=4 et
#include <string>

#include <fmt/core.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/lustre.h>

#include "repo.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

void repo_args::add_cli(CLI::App& cli,
                        [[maybe_unused]] global_settings& settings) {
    auto* repo_cli =
        cli.add_subcommand("repo", "manage and query uenv image repositories");

    // add the create command, i.e. `uenv repo create ...`
    auto* create_cli =
        repo_cli->add_subcommand("create", "create a new uenv repository");

    create_cli->add_option("path", create_args.path,
                           "path of the repo to create");
    create_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::repo_create; });

    // add the status command, i.e. `uenv repo status ...`
    auto* status_cli = repo_cli->add_subcommand(
        "status", "status of an existing uenv repository");
    status_cli->add_option("path", status_args.path, "path of the repo");
    status_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::repo_status; });

    // add the update command, i.e. `uenv repo update ...`
    auto* update_cli = repo_cli->add_subcommand(
        "update", "update an existing uenv repository");

    update_cli->add_option("path", status_args.path, "path of the repo");
    update_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::repo_update; });
}

// inspect the repo path that is optionally passed as an argument.
// if no argument is provided, fall back to the value passed using
// the --repo argument, which in turn falls back to the default value.
util::expected<std::string, std::string>
resolve_repo_path(std::optional<std::string> path,
                  const global_settings& settings) {
    if (path) {
        if (auto result = parse_path(*path); !result) {
            return util::unexpected(result.error().message());
        }
        return *path;
    }
    if (settings.config.repo) {
        return settings.config.repo.value();
    }
    return util::unexpected("no repo path provided");
}

int repo_create(const repo_create_args& args, const global_settings& settings) {
    auto path = resolve_repo_path(args.path, settings);
    if (!path) {
        term::error("invalid repository path: {}", path.error());
        return 1;
    }
    spdlog::info("attempting to create uenv repo at {}", *path);
    auto x = create_repository(*path);
    if (!x) {
        term::error("{}", x.error());
        return 1;
    }
    return 0;
}

int repo_status(const repo_status_args& args, const global_settings& settings) {
    using enum repo_state;

    auto path = resolve_repo_path(args.path, settings);
    if (!path) {
        term::error("invalid repository path: {}", path.error());
        return 1;
    }

    auto status = validate_repository(*path);
    if (status == readonly) {
        term::msg("the repository at {} is read only\n", *path);
    }
    if (status == readwrite) {
        term::msg("the repository at {} is read-write\n", *path);
    }
    if (status == no_exist) {
        term::msg("no repository at {}\n", *path);
    }
    if (status == invalid) {
        term::msg("the repository at {} is in invalid state\n", *path);
    }

    // check for lustre striping
    if (status != invalid) {
        if (auto p = lustre::load_path(*path, settings.calling_environment)) {
            auto state = lustre::is_striped(*p);
            if (!state) {
                term::msg("the repository is on a lustre file system and is "
                          "not striped");
                term::msg("\n{}", state);
                term::msg("\nrun '{}' to apply striping to the repository",
                          color::yellow("uenv repo update"));
            } else {
                term::msg("the repository is a striped lustre file system");
            }
        } else {
            term::msg("the repository is not a lustre file system");
        }
    }

    return 0;
}

int repo_update(const repo_status_args& args, const global_settings& settings) {
    using enum repo_state;

    auto path = resolve_repo_path(args.path, settings);
    if (!path) {
        term::error("invalid repository path: {}", path.error());
        return 1;
    }

    auto status = validate_repository(*path);
    if (status == readonly) {
        term::error("the repository at {} is read only\n", *path);
        return 1;
    }
    if (status == no_exist) {
        term::error("no repository at {}\n", *path);
        return 1;
    }
    if (status == invalid) {
        term::error("the repository at {} is in invalid state\n", *path);
        return 1;
    }

    if (auto p = lustre::load_path(*path, settings.calling_environment)) {
        if (!lustre::is_striped(*p)) {
            term::msg("{} is on a lustre file system and is not striped",
                      p->path.string());
            lustre::set_striping(*p, lustre::default_striping);
        }
    }

    return 0;
}

} // namespace uenv
