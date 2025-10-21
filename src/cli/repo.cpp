// vim: ts=4 sts=4 sw=4 et
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
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
    status_cli->add_flag("--json", status_args.json, "output in json format");
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

    using nlohmann::json;
    json json_out;

    auto path = resolve_repo_path(args.path, settings);
    if (!path) {
        term::error("invalid repository path: {}", path.error());
        return 1;
    }

    json_out["path"] = *path;
    json_out["fstype"] = "unknown";
    json_out["updates"] = json::array();
    json_out["digest-remove"] = json::array();

    auto status = validate_repository(*path);
    if (!(status == readonly || status == readwrite)) {
        if (args.json) {
            json_out["status"] = status == invalid ? "invalid" : "noexist";
            term::msg("{}", json_out.dump());
        } else {
            term::msg("the repository {} {}.", *path,
                      status == invalid ? "is invalid" : "does not exist");
        }
        return 0;
    } else {
        json_out["status"] = status == readwrite ? "readwrite" : "readonly";
    }

    // set to true when an update can be applied using `uenv repo update`
    bool update = false;

    // check for lustre striping
    if (auto p = lustre::load_path(*path, settings.calling_environment)) {
        auto state = lustre::is_striped(*p);
        json_out["fstype"] = "lustre";
        if (!state) {
            if (!args.json) {
                term::msg("the repository {} is on a lustre file system is not "
                          "striped",
                          *path);
                term::msg("\n{}", state);
            }
            json_out["updates"].push_back("lustre-striping");
            update = true;
        }
    }

    // check for inconsistencies between stored images and the database
    if (auto store = uenv::open_repository(*path)) {
        if (auto C = impl::check_repo_consistency(*store); !C) {
            if (!args.json) {
                term::msg("the repository {} has missing uenv images:", *path);
            }
            json_out["updates"].push_back("storage");
            for (auto& [digest, records] : C.no_storage) {
                auto jlabels = json::array();
                for (auto& r : records) {
                    if (!args.json) {
                        term::msg("  {} {}", digest, r);
                    }
                    jlabels.push_back(fmt::format("{}", r));
                }
                json_out["digest-remove"].push_back(
                    {{"digest", fmt::format("{}", digest)},
                     {"labels", jlabels}});
            }
            update = true;
        }
    } else {
        term::error("the repository at {} could not be opened {}", *path,
                    store.error());
    }

    if (!args.json && update) {
        term::msg("\nrun '{}' to apply updates to the repository {}",
                  color::yellow("uenv repo update"), *path);
    }

    if (args.json) {
        term::msg("{}", json_out.dump());
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
            lustre::set_striping(*p, lustre::default_striping, true);
        }
    }

    // check for inconsistencies between stored images and the database
    if (auto store = uenv::open_repository(*path, uenv::repo_mode::readwrite)) {
        if (auto C = impl::check_repo_consistency(*store); !C) {
            term::msg("the repository at {} has missing uenv images:", *path);
            for (auto& [digest, records] : C.no_storage) {
                term::msg("  deleting {}...", digest);
                const auto store_path = store->uenv_paths(digest).store;
                if (std::filesystem::is_directory(store_path)) {
                    try {
                        std::filesystem::remove_all(store_path);
                    } catch (const std::exception& e) {
                        term::error("unable to delete {}: {}", store_path,
                                    e.what());
                    }
                    spdlog::debug("removed path {}", store_path);
                }
                store->remove(digest);
                spdlog::debug("removed record {}", digest);
            }
        }
    } else {
        term::error("the repository at {} could not be opened {}", *path,
                    store.error());
    }

    return 0;
}

} // namespace uenv
