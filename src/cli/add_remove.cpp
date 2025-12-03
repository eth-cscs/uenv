// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/subprocess.h>

#include "add_remove.h"
#include "help.h"
#include "terminal.h"
#include "util.h"

namespace uenv {

std::string image_add_footer();
std::string image_rm_footer();

void image_add_args::add_cli(CLI::App& cli,
                             [[maybe_unused]] global_settings& settings) {
    auto* add_cli =
        cli.add_subcommand("add", "add a uenv image to a repository");
    add_cli
        ->add_option("label", uenv_description,
                     "the label, of the form name/version:tag@system%uarch")
        ->required();
    add_cli->add_flag("--move", move,
                      "move the squahfs image instead of copying it.");
    add_cli->add_option("squashfs", squashfs, "the squashfs file to add.")
        ->required();
    add_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_add; });

    add_cli->footer(image_add_footer);
}

void image_rm_args::add_cli([[maybe_unused]] CLI::App& cli,
                            [[maybe_unused]] global_settings& settings) {
    auto* rm_cli =
        cli.add_subcommand("rm", "delete a uenv image from a repository");
    rm_cli->add_option("label", uenv_description, "the uenv to remove.")
        ->required();
    rm_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_rm; });

    rm_cli->footer(image_rm_footer);
}

int image_add(const image_add_args& args, const global_settings& settings) {
    namespace fs = std::filesystem;

    //
    // parse the cli args
    //
    // - the squashfs file
    // - the label
    auto label = uenv::parse_uenv_label(args.uenv_description);
    if (!label) {
        term::error("the label {} is not valid: {}", args.uenv_description,
                    label.error().message());
        return 1;
    }
    if (!label->fully_qualified()) {
        term::error(
            "the label {} must provide at name/version:tag@system%uarch",
            args.uenv_description);
        return 1;
    }

    spdlog::info("image_add: label {}", *label);

    const auto env = concretise_env(args.squashfs, {}, settings.config.repo,
                                    settings.calling_environment);
    if (!env) {
        term::error("{}", env.error());
        return 1;
    }
    if (env->uenvs.size() != 1) {
        term::error("Too many arguments provided for source squashfs file");
        return 1;
    }

    auto sqfs =
        uenv::validate_squashfs_image(env->uenvs.begin()->second.sqfs_path);
    if (!sqfs) {
        term::error("invalid squashfs file {}: {}", args.squashfs,
                    sqfs.error());
        return 1;
    }
    spdlog::info("image_add: squashfs {}", sqfs.value());

    //
    // Open the repository
    //
    if (!settings.config.repo) {
        term::error("a repo needs to be provided either using the --repo "
                    "option, or in the config file");
        return 1;
    }
    auto store = uenv::open_repository(settings.config.repo.value(),
                                       repo_mode::readwrite);
    if (!store) {
        term::error("unable to open repo: {}", store.error());
        return 1;
    }

    //
    // check whether the label also exists
    //
    bool existing_label = false;
    {
        auto results = store->query(*label);
        if (!results) {
            term::error(
                "image_add: internal error search repository for {}\n  {}",
                *label, results.error());
        }
        existing_label = !results->empty();

        if (existing_label) {
            term::error("image_add: a uenv already exists with the label {}",
                        *label);
            return 1;
        }
    }

    //
    // check whether repository already contains an image with the same
    // hash
    //
    bool existing_hash = false;
    {
        uenv_label hash_label{sqfs->hash};
        auto results = store->query(hash_label);
        if (!results) {
            term::error(
                "image_add: internal error search repository for {}\n  {}",
                *label, results.error());
        }
        existing_hash = !results->empty();

        if (existing_hash) {
            spdlog::warn("a uenv with the same sha {} is already in the repo",
                         sqfs->hash);
            term::warn("a uenv with the same sha {} is already in the repo",
                       sqfs->hash);
        }
    }

    const auto uenv_paths = store->uenv_paths(sqfs->hash);
    uenv::uenv_date date{*util::file_creation_date(sqfs->sqfs)};

    bool source_in_repo =
        util::is_child(sqfs->sqfs, settings.config.repo.value());
    if (!source_in_repo) {
        //
        // create the path inside the repo
        //
        std::error_code ec;
        // if the path exists, delete it, as it might contain a partial download
        if (fs::exists(uenv_paths.store)) {
            spdlog::debug("image_add: remove the target path {} before copying",
                          uenv_paths.store.string());
            fs::remove_all(uenv_paths.store);
        }

        fs::create_directories(uenv_paths.store, ec);
        if (ec) {
            spdlog::error("unable to create path {}: {}",
                          uenv_paths.store.string(), ec.message());
            term::error("unable to add the uenv");
            return 1;
        }

        //
        // copy the meta data into the repo
        //
        if (sqfs->meta) {
            fs::copy_options options{};
            options |= fs::copy_options::recursive;
            fs::copy(sqfs->meta.value(), uenv_paths.meta, options, ec);
            if (ec) {
                spdlog::error("unable to copy meta data to {}: {}",
                              uenv_paths.meta.string(), ec.message());
                term::error("unable to add the uenv");
                return 1;
            }
        }

        // copy or move the
        if (!args.move) {
            fs::copy_file(sqfs->sqfs, uenv_paths.squashfs, ec);
            if (ec) {
                spdlog::error("unable to copy squashfs image {} to {}: {}",
                              sqfs->sqfs.string(), uenv_paths.squashfs.string(),
                              ec.message());
                term::error("unable to add the uenv");
                return 1;
            }
        } else {
            fs::rename(sqfs->sqfs, uenv_paths.squashfs, ec);
            if (ec) {
                spdlog::error("unable to move squashfs image {} to {}: {}",
                              sqfs->sqfs.string(), uenv_paths.squashfs.string(),
                              ec.message());
                term::error("unable to add the uenv\n{}",
                            help::item{help::block{
                                help::block::admonition::note,
                                fmt::format("check that the file {} is on the "
                                            "same filesystem as "
                                            "the repository, and that you have "
                                            "write access to it.",
                                            sqfs->sqfs.string())}});
                return 1;
            }
        }
    }

    //
    // add the uenv to the database
    //
    if (!date.validate()) {
        spdlog::error("the date {} is invalid", date);
        term::error("unable to add the uenv");
        return 1;
    }
    uenv_record r{
        *label->system,
        *label->uarch,
        *label->name,
        *label->version,
        *label->tag,
        date,
        fs::file_size(uenv_paths.squashfs),
        sqfs->hash,
        sqfs->hash.substr(0, 16),
    };
    if (auto result = store->add(r); !result) {
        spdlog::error("image_add: {}", result.error());
        term::error("unable to add the uenv");
        return 1;
    }
    term::msg("the uenv {} with sha {} was added to {}", r, sqfs->hash,
              store->path()->string());

    return 0;
}

int image_rm([[maybe_unused]] const image_rm_args& args,
             [[maybe_unused]] const global_settings& settings) {
    spdlog::info("image rm {}", args.uenv_description);

    // open the repo
    if (!settings.config.repo) {
        term::error("a repo needs to be provided either using the --repo flag "
                    "in the config file");
        return 1;
    }
    auto store = uenv::open_repository(settings.config.repo.value(),
                                       repo_mode::readwrite);
    if (!store) {
        term::error("unable to open repo: {}", store.error());
        return 1;
    }

    // Step 1: find the record/uenv to remove from the local repository
    //
    // if sha is set:
    //      remove the sha and underlying image
    // if record is set
    //      remove the record, but leave the sha/image in place because
    //      more than one record
    // if neither is set:
    //      do nothing

    std::optional<sha256> sha;
    std::optional<uenv_record> record;
    auto U = args.uenv_description;

    // check if the CLI argument is a sha256
    if (is_sha(U, 64)) {
        spdlog::debug("image_rm: treating {} as a sha256", U);
        // look it up in the database
        if (auto r = store->query({.name = U})) {
            if (!r->empty()) {
                sha = U;
            } else {
                term::error("no uenv matches {}", U);
                return 1;
            }
        } else {
            term::error("internal error");
            return 1;
        }
    }
    // check if the CLI argument is an id
    else if (is_sha(U, 16)) {
        spdlog::debug("image_rm: treating {} as an id", U);
        // look it up in the database
        if (auto r = store->query({.name = U})) {
            if (!r->empty()) {
                sha = r->begin()->sha;
            } else {
                term::error("no uenv matches {}", U);
                return 1;
            }
        } else {
            term::error("internal error");
            return 1;
        }
    }
    // otherwise treat the CLI argument as a uenv label
    else {
        spdlog::debug("image_rm: treating {} as a label", U);
        auto label = uenv::parse_uenv_label(U);
        if (!label) {
            spdlog::error("the label {} is not valid: {}", U,
                          label.error().message());
            term::error("the label {} is not valid: {}", U,
                        label.error().message());
            return 1;
        }
        if (!label->partially_qualified()) {
            spdlog::error("no uenv matches {}", U);
            term::error(
                "the label {} does not provide at least name/version:tag", U);
            return 1;
        }
        spdlog::info("image_rm: label {}", *label);

        if (auto r = store->query(*label)) {
            if (r->empty()) {
                spdlog::error("no uenv matches {}", U);
                term::error("no uenv matches {}", U);
                return 1;
            } else if (r->size() > 1) {
                term::error("the pattern {} matches more than one "
                            "uenv:\n{}use a more specific version",
                            U, format_record_set(*r));
                return 1;
            } else {
                // check whether there are more than one tag attached to sha
                if (store->query({.name = r->begin()->sha.string()})->size() >
                    1) {
                    record = *r->begin();
                } else {
                    sha = r->begin()->sha;
                }
            }
        }
    }

    // Step 2: perform deletion

    record_set removed;

    if (sha) {
        spdlog::info("removing sha {}", *sha);

        removed = *store->remove(*sha);

        auto store_path = store->uenv_paths(*sha).store;
        if (std::filesystem::exists(store_path)) {
            spdlog::info("image_rm: deleting path {}", store_path.string());
            std::filesystem::remove_all(store_path);
        } else {
            spdlog::warn("image_rm: the path {} does not exist - skipping",
                         store_path.string());
        }
    } else if (record) {
        spdlog::info("removing record {}", *record);

        removed = *store->remove(*record);
    }

    if (removed.empty()) {
        term::msg("no uenv matching {} was found", U);
    } else {
        term::msg("the following uenv {} removed:",
                  (removed.size() > 1 ? "were" : "was"));
        print_record_set(removed, true);
    }

    return 0;
}

std::string image_add_footer() {
    using enum help::block::admonition;
    using help::lst;

    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Add a uenv image to a repository." },
        help::linebreak{},
        help::block{xmpl, "add an image to the default repository:"},
        help::block{code,   "uenv image add myenv/24.7:v1@todi%gh200 ./store.squashfs"},
        help::block{none, fmt::format("the label must be of the complete {} form.", lst("name/version:tag@system%uarch"))},
        help::linebreak{},
        help::block{xmpl, "add an image by moving the input image into the repository:"},
        help::block{code,   "uenv image add --move myenv/24.7:v1@todi%gh200 ./store.squashfs"},
        help::block{none, "this method is significantly faster for large image files, however it should"},
        help::block{none, "only be used when the original input squashfs file is no longer needed."},

        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

std::string image_rm_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Remove a uenv image from a repository." },
        help::block{none, "Use this command to remove uenv that have been pulled or added." },
        help::linebreak{},
        help::block{xmpl, "by label"},
        help::block{code,   "uenv image rm prgenv-gnu/24.7:v1"},
        help::block{code,   "uenv image rm prgenv-gnu/24.7:v1@daint%gh200"},
        help::linebreak{},
        help::block{xmpl, "by sha"},
        help::block{code,   "uenv image rm abcd1234abcd1234abcd1234abcd1234"},
        help::linebreak{},
        help::block{xmpl, "by id"},
        help::block{code,   "uenv image rm abcd1234"},
        help::linebreak{},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
