// vim: ts=4 sts=4 sw=4 et

#include <fstream>
#include <memory>
#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <oci/digest.h>
#include <oci/manifest.h>
#include <uenv/env.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/sha.h>
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
        ->add_option("label", label,
                     "the label of the uenv created in the repo, of the form "
                     "name/version:tag@system%uarch")
        ->required();
    add_cli->add_flag("--move", move,
                      "move the squahfs image instead of copying it.");
    add_cli
        ->add_option("uenv", source,
                     "the label or squashfs file to add to the repo.")
        ->required();
    add_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_add; });

    add_cli->footer(image_add_footer);
}

void image_rm_args::add_cli([[maybe_unused]] CLI::App& cli,
                            [[maybe_unused]] global_settings& settings) {
    auto* rm_cli =
        cli.add_subcommand("rm", "delete a uenv image from a repository");
    rm_cli->add_option("uenv", label, "the uenv to remove.")->required();
    rm_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_rm; });

    rm_cli->footer(image_rm_footer);
}

int image_add(const image_add_args& args, const global_settings& settings) {
    namespace fs = std::filesystem;
    spdlog::info("image_add: trying to add uenv {} with label {} ... {}",
                 args.source, args.label, args);

    //
    // parse the cli args
    //

    // the label is the label that will be given to the uenv added to the repo
    auto label = uenv::parse_uenv_label(args.label);
    if (!label) {
        term::error("the label {} is not valid: {}", args.label,
                    label.error().message());
        return 1;
    }
    if (!label->fully_qualified()) {
        term::error(
            "the label {} must provide at name/version:tag@system%uarch",
            args.label);
        return 1;
    }

    // the source can be either the path of a squashfs file, or a label
    uenv_description source;
    if (const auto parse = parse_uenv_description(args.source); !parse) {
        term::error("invalid uenv specification: {}", parse.error().message());
        return 1;
    } else {
        source = parse.value();
    }

    // derive the full description of the source uenv.
    const auto env = resolve_uenv(source, settings.config.repos);
    if (!env) {
        term::error("{}", env.error());
        return 1;
    }

    const bool from_label = (bool)env->record;

    util::expected<squashfs_image, std::string> sqfs;
    if (from_label) {
        sqfs = squashfs_image{env->sqfs_path, env->meta_path, env->record->sha};
    } else {
        // hashing the image reads the whole file, so show progress for it. The
        // bar is created on the first callback, once the size is known.
        std::unique_ptr<uenv::transfer_bar> prepare_bar;
        sqfs = uenv::validate_squashfs_image(
            env->sqfs_path,
            [&prepare_bar, &env](std::uint64_t done, std::uint64_t total) {
                if (!prepare_bar) {
                    prepare_bar = uenv::make_transfer_bar(
                        total, fmt::format("preparing {}",
                                           env->sqfs_path.filename().string()));
                }
                prepare_bar->update(done);
            });
        // stop the bar before anything else is printed, so that an error
        // message does not land on the bar's line.
        if (prepare_bar) {
            prepare_bar->finish();
        }
        if (!sqfs) {
            term::error("invalid source {}: {}", args.source, sqfs.error());
            return 1;
        }
    }

    spdlog::info("image_add: trying to add uenv {} with label {}", args.source,
                 *label);

    //
    // if this is a newly added squashfs file (not a retag of an existing
    // repo entry), mint the manifest that identifies it: its sha256 becomes
    // the <hash> used to store and index it, and it is persisted alongside
    // the image so that a later `uenv image push` can reuse it verbatim
    // instead of minting a new one. `created` is derived from the squashfs
    // file's own creation date, rather than the current time, so that
    // re-adding identical squashfs content always mints the same manifest
    // (and hence hash), preserving the existing_hash dedup check below.
    //
    sha256 image_hash = sqfs->hash;
    std::optional<std::string> manifest_body;
    if (!from_label) {
        auto created = oci::rfc3339(*util::file_creation_date(sqfs->sqfs));
        auto m = oci::make_squashfs_manifest(oci::digest::sha256(sqfs->hash),
                                             fs::file_size(sqfs->sqfs),
                                             created);
        manifest_body = oci::serialize_manifest(m);
        spdlog::debug("image_add: creating manifest {}", *manifest_body);

        image_hash = util::sha256_string(*manifest_body);
        spdlog::debug("image_add: manifest hash {}", image_hash);
    }

    // Open the repository
    auto store = uenv::concretise_user_repo(settings.config);
    if (!store) {
        term::error("unable to open repo: {}", store.error());
        return 1;
    }

    spdlog::debug("image_add: using repo {}", store->path());

    // check whether the label already exists in the repo
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

    // check whether the hash already exists in the repo

    const auto uenv_paths = store->uenv_paths(image_hash);
    const bool source_in_repo = util::is_child(sqfs->sqfs, uenv_paths.root);

    // If a sqfs file is already in repo then it must be added using:
    //   uenv image add <new-label> <existing-label>
    if (source_in_repo && !from_label) {
        term::error("image_add: Trying to add a squashfs file which is already "
                    "in the repository. Add using a label instead of squashfs.");
        return 1;
    }

    // check whether repository already contains an image with the same
    // hash
    {
        uenv_label hash_label{image_hash.string()};
        auto results = store->query(hash_label);
        if (!results) {
            term::error(
                "image_add: internal error search repository for {}\n  {}",
                *label, results.error());
        }
        const auto existing_hash = !results->empty();

        if (existing_hash && !from_label) {
            term::error("a uenv with the same sha {} is already in the repo",
                       image_hash);
            return 1;
        }
    }

    // If copying the squashfs file into the repository
    // Create the path <repo>/images/<hash> and populate with meta data, manifest
    // and the image iteself.
    if (!source_in_repo) {
        // create the path inside the repo
        std::error_code ec;
        // if the path exists, delete it: it was probably caused by an aborted
        // `image add` or `image pull` command that did not complete the download
        // and database update.
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

        spdlog::debug("image_add: repo path {}", uenv_paths.store);

        // copy the meta data into the repo
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
            spdlog::debug("image_add: added meta path {}", sqfs->meta.value());
        }

        // copy or move the squashfs file
        if (!args.move) {
            spdlog::debug("image_add: copying {} to {}", sqfs->sqfs, uenv_paths.squashfs);
            // record the source modification time so that it can be preserved
            // on the copy: fs::copy_file stamps the destination with the current
            // time, whereas fs::rename (the move case) keeps the source time.
            std::error_code tec;
            const auto src_time = fs::last_write_time(sqfs->sqfs, tec);
            fs::copy_file(sqfs->sqfs, uenv_paths.squashfs, ec);
            if (ec) {
                spdlog::error("unable to copy squashfs image {} to {}: {}",
                              sqfs->sqfs.string(), uenv_paths.squashfs.string(),
                              ec.message());
                term::error("unable to add the uenv");
                return 1;
            }
            // preserve the source modification time on the copy so that the
            // creation date recorded in the repository matches the source.
            if (!tec) {
                fs::last_write_time(uenv_paths.squashfs, src_time, tec);
            }
            if (tec) {
                spdlog::warn(
                    "image_add: unable to preserve modification time on {}: {}",
                    uenv_paths.squashfs.string(), tec.message());
            }
        } else {
            spdlog::debug("image_add: moving {} to {}", sqfs->sqfs, uenv_paths.squashfs);
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

        // add the manifest alongside the image
        if (manifest_body) {
            std::ofstream mfid(uenv_paths.manifest);
            mfid << *manifest_body;
            if (!mfid) {
                spdlog::error("unable to write manifest to {}",
                              uenv_paths.manifest.string());
                term::error("unable to add the uenv");
                return 1;
            }
        }
    }

    // add the label to the database
    // read the creation date from the image in its final destination in the
    // repository: in the --move case the source path no longer exists, and in
    // both cases the in-repo copy is the authoritative file.
    const uenv::uenv_date date{*util::file_creation_date(uenv_paths.squashfs)};
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
        image_hash,
        uenv_id::parse(image_hash.string().substr(0, 16)).value(),
    };
    if (auto result = store->add(r); !result) {
        spdlog::error("image_add: {}", result.error());
        term::error("unable to add the uenv");
        return 1;
    }
    term::msg("the uenv {} with sha {} was added to {}", r, image_hash,
              store->path()->string());

    return 0;
}

int image_rm([[maybe_unused]] const image_rm_args& args,
             [[maybe_unused]] const global_settings& settings) {
    spdlog::info("image rm {}", args.label);

    // find/create and open the default repository
    auto store = uenv::concretise_user_repo(settings.config);
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
    auto U = args.label;

    // check if the CLI argument is a sha256
    if (auto parsed = sha256::parse(U)) {
        spdlog::debug("image_rm: treating {} as a sha256", U);
        // look it up in the database
        if (auto r = store->query({.name = U})) {
            if (!r->empty()) {
                sha = *parsed;
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
    else if (uenv_id::parse(U)) {
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
                            U,
                            format_record_set_format(
                                *r, "{name}/{version}:{tag}@{system}%{uarch}"));
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
        print_record_set(removed, record_set_format::list);
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
