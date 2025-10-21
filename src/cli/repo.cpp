// vim: ts=4 sts=4 sw=4 et
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/lustre.h>

#include "help.h"
#include "repo.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

std::string repo_footer();

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
    status_cli->add_flag("--json", status_args.json, "output in json format");
    status_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::repo_status; });

    // add the update command, i.e. `uenv repo update ...`
    auto* update_cli = repo_cli->add_subcommand(
        "update", "update an existing uenv repository");

    update_cli->add_option("path", status_args.path, "path of the repo");
    update_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::repo_update; });

    repo_cli->footer(repo_footer);
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

struct repo_consistency {
    // a list of all digests that have missing squashfs or store paths
    std::unordered_map<uenv::sha256, std::vector<uenv::uenv_record>> no_storage;

    // return false if the repository is not consistent
    operator bool() const {
        return no_storage.empty();
    }
};

namespace impl {

// check a repository for missing squashfs files, which can be caused by user
// intervention or file system deletion policies.
repo_consistency check_repo_consistency(const uenv::repository& store) {
    namespace fs = std::filesystem;

    // an empty query will return all uenv for all systems
    const auto query = store.query({});
    if (!query) {
        term::error("unable to query database: {}", query.error());
        return {};
    }

    repo_consistency R{};

    // for recording visited digests
    std::set<uenv::sha256> shas;
    for (auto& r : *query) {
        const auto digest = r.sha;
        // if the digest has not been encountered, test whether the
        // corresponding squashfs storage exists
        if (!shas.contains(digest)) {
            shas.insert(digest);

            auto p = store.uenv_paths(digest);

            // record the digest if the storage path or squashfs file is missing
            if (!fs::is_directory(p.store) ||
                !fs::is_regular_file(p.squashfs)) {
                spdlog::trace("check_repo_consistency:: {} does not exist",
                              p.squashfs);
                R.no_storage[digest].push_back(r);
            } else {
                spdlog::trace("check_repo_consistency:: {} exists", p.squashfs);
            }
        }
        // the digest has already been checked
        else {
            // add the record to the list of records to be deleted if the digest
            // has been marked for removal
            if (R.no_storage.count(digest) > 0) {
                R.no_storage[digest].push_back(r);
            }
        }
    }

    return R;
}

} // namespace impl

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

// JSON outpu will generate a json string like the following
// {
//   "path": "/scratch/.repo",
//   "status": "readwrite",
//   "fstype": "scratch",
//   // list of chanages that can be applied
//   "updates": ["lustre-striping", "storage"],
//   // list of digests to remove (default is empty)
//   "digest-remove": [
//       {
//        "digest": "aa789ssdf",
//        "labels": ["bilby/23:v2@daint"]
//       }
//   ],
// }

int repo_status(const repo_status_args& args, const global_settings& settings) {
    using enum repo_state;

    auto path = resolve_repo_path(args.path, settings);
    if (!path) {
        term::error("invalid repository path: {}", path.error());
        return 1;
    }

    auto status = validate_repository(path.value());

    const bool valid_repo = status == readonly || status == readwrite;
    bool update = false;

    // set to true when an update can be applied using `uenv repo update`
    std::optional<lustre::stripe_stats> lustre_state{};
    std::optional<repo_consistency> store_state{};

    if (valid_repo) {
        // check for lustre striping
        if (auto p =
                lustre::load_path(path.value(), settings.calling_environment)) {
            lustre_state = lustre::is_striped(*p);
            update |= lustre_state.value();
        }

        // check for inconsistencies between stored images and the database
        if (auto store = uenv::open_repository(path.value())) {
            if (auto c = impl::check_repo_consistency(store.value()); !c) {
                store_state = c;
                update = true;
            }
        } else {
            term::error("the repository at {} could not be opened {}",
                        path.value(), store.error());
            return 1;
        }
    }

    // output the results as json
    if (args.json) {
        using nlohmann::json;
        json json_out;
        json_out["path"] = path.value();
        json_out["fstype"] = lustre_state ? "lustre" : "unknown";
        if (lustre_state && !lustre_state.value()) {
            json_out["updates"].push_back("lustre-striping");
        }
        json_out["updates"] = json::array();
        json_out["digest-remove"] = json::array();
        json_out["status"] = fmt::format("{}", status);
        if (store_state) {
            json_out["updates"].push_back("storage");
            for (const auto& [digest, records] : store_state->no_storage) {
                auto jlabels = json::array();
                for (const auto& r : records) {
                    jlabels.push_back(fmt::format("{}", r));
                }
                json_out["digest-remove"].push_back(
                    {{"digest", fmt::format("{}", digest)},
                     {"labels", jlabels}});
            }
        }

        term::msg("{}", json_out.dump());
    }
    // output the results in human readable form (the default)
    else {
        if (status == no_exist) {
            term::msg("{} is not a repository", path.value());
        } else {
            term::msg("the repository {} is {}", path.value(), status);
        }
        if (lustre_state) {
            if (!lustre_state.value()) {
                term::msg("  - is on a lustre file system that is not striped",
                          path.value());
            } else {
                term::msg("  - on a lustre file system");
            }
        }
        if (store_state) {
            term::msg("  - has missing uenv images:");
            for (const auto& [digest, records] : store_state->no_storage) {
                for (const auto& r : records) {
                    term::msg("    {} {}", digest, r);
                }
            }
        }
        if (update) {
            term::msg("\nrun '{}' to apply updates to the repository",
                      color::yellow(
                          fmt::format("uenv repo update {}", path.value())));
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
            term::msg("{} applying striping", p->path.string());
            lustre::set_striping(*p, lustre::default_striping, true);
        }
    }

    // check for inconsistencies between stored images and the database
    if (auto store = uenv::open_repository(*path, uenv::repo_mode::readwrite)) {
        if (auto C = impl::check_repo_consistency(*store); !C) {
            term::msg("the repository at {} has missing uenv images:", *path);
            for (auto& [digest, records] : C.no_storage) {
                term::msg("  removing stale ref {}", digest);
                const auto store_path = store->uenv_paths(digest).store;
                if (std::filesystem::is_directory(store_path)) {
                    try {
                        std::filesystem::remove_all(store_path);
                        spdlog::debug("removed path {}", store_path);
                    } catch (const std::exception& e) {
                        term::error("unable to delete {}: {}", store_path,
                                    e.what());
                    }
                }
                store->remove(digest);
                spdlog::debug("removed record {}", digest);
            }
        }
    } else {
        term::error("the repository at {} could not be opened {}", *path,
                    store.error());
        return 1;
    }

    term::msg("The repository {} is up to date", *path);

    return 0;
}

std::string repo_footer() {
    using enum help::block::admonition;
    using help::block;
    using help::linebreak;
    using help::lst;
    std::vector<help::item> items{
        // clang-format off
        block{none, "Create, query and update uenv image repositories."},
        linebreak{},
        block{none, "Uenv repositories (repos) are a directory on the file system that contains"},
        block{none, "an sqlite database with information about uenv in the repo and the squashfs"},
        block{none, "files for each uenv in the repo."},
        linebreak{},
        block{note, "The default location for the uenv repository is in $SCRATCH/.uenv-images if SCRATCH"},
        block{none, "is defined, otherwise $HOME/.uenv."},
        block{none, "An alternative default location can be set in the uenv configuration file."},
        linebreak{},
        block{xmpl, "To get the status of a repository"},
        block{code,   "# status of the default repository"},
        block{code,   "uenv repo status"},
        block{code,   "# pass the path of a repository as an additional argument"},
        block{code,   "uenv repo status $HOME/custom-repo"},
        linebreak{},
        block{note, "The repo status command provides information including whether"},
        block{none, "  - it is read-only or read-write"},
        block{none, "  - it is on a lustre file system"},
        block{none, "  - its contents are out of synch with the database"},
        block{none, "  - lustre striping has been applied to its contents"},
        block{none, "See the repo update command to synchronise the database or apply striping on lustre."},
        linebreak{},
        block{xmpl, "The --json flag returns output in JSON format for integration into tools and scripts"},
        block{code,   "uenv repo status --json"},
        linebreak{},
        block{xmpl, "The 'repo create' sub-command creates a new empty repository:"},
        block{code,   "uenv repo create $HOME/my-repo"},
        block{none, "will create a new repository at $HOME/my-repo."},
        linebreak{},
        block{note, "The path argument is optional: if it is not passed uenv will attempt to create"},
        block{none, "a repo in the default location."},
        linebreak{},
        block{xmpl, "The 'repo update' sub-command applies updates and upgrades to a repository:"},
        block{code,   "uenv repo update"},
        block{none, "will update the default repo."},
        linebreak{},
        block{note, "Updates are used to upgrade or update a repository, when needed. Currently two upgrades"},
        block{none, "are applied:"},
        block{none, "  - apply Lustre striping if the repo is on a Lustre file system and no striping"},
        block{none, "    has already been applied."},
        block{none, "  - remove uenv from the database if their squashfs file does not exist"},
        block{none, "    which can occur to repos on non-default locations that are subject to"},
        block{none, "    clean up policies."},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
