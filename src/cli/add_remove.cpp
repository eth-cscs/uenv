// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/cscs.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/subprocess.h>

#include "add_remove.h"
#include "help.h"

namespace uenv {

std::string image_add_footer();
std::string image_remove_footer();

void image_add_args::add_cli(CLI::App& cli,
                             [[maybe_unused]] global_settings& settings) {
    auto* add_cli =
        cli.add_subcommand("add", "add a uenv image to a repository");
    add_cli
        ->add_option("label", uenv_description,
                     "the label, of the form name:version:tag@system#uarch")
        ->required();
    add_cli->add_flag("--move", move,
                      "move the squahfs image instead of copying it.");
    add_cli->add_option("squashfs", squashfs, "the squashfs file to add.")
        ->required();
    add_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_add; });

    add_cli->footer(image_add_footer);
}

void image_remove_args::add_cli([[maybe_unused]] CLI::App& cli,
                                [[maybe_unused]] global_settings& settings) {
    /*
    auto* remove_cli =
        cli.add_subcommand("remove", "delete a uenv image from a repository");
    remove_cli->add_option("uenv", uenv_description, "the uenv to remove.");
    remove_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_remove; });

    remove_cli->footer(image_remove_footer);
    */
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
        spdlog::error("the label {} is not valid: {}", args.uenv_description,
                      label.error().description);
        return 1;
    }
    if (!(label->name && label->version && label->tag)) {
        spdlog::error("the label {} must provide at a minimum name/version:tag",
                      args.uenv_description);
        return 1;
    }
    spdlog::info("image_add: label {}", *label);

    auto file = uenv::parse_path(args.squashfs);
    if (!file) {
        spdlog::error("invalid squashfs file {}: {}", args.squashfs,
                      file.error().description);
        return 1;
    }
    fs::path sqfs = *file;
    if (!fs::is_regular_file(sqfs)) {
        spdlog::error("invalid squashfs file {} is not a file", args.squashfs);
        return 1;
    }
    spdlog::info("image_add: squashfs {}", fs::absolute(sqfs));

    //
    // get the sha256 of the file
    //
    std::string hash;
    {
        auto proc = util::run({"sha256sum", sqfs.string()});
        if (!proc) {
            spdlog::info("{}", proc.error());
            spdlog::error("unable to calculate sha256 of squashfs file {}",
                          args.squashfs);
            return 1;
        }
        auto success = proc->wait();
        if (success != 0) {
            spdlog::error("unable to calculate sha256 of squashfs file {}",
                          args.squashfs);
            return 1;
        }

        auto raw = *proc->out.getline();

        hash = raw.substr(0, 64);
    }
    spdlog::debug("image_add: squashfs hash {}", hash);

    //
    // Open the repository
    //
    if (!settings.repo) {
        spdlog::error(
            "a repo needs to be provided either using the --repo flag or by "
            "setting the UENV_REPO_PATH environment variable");
        return 1;
    }
    auto store =
        uenv::open_repository(settings.repo.value(), repo_mode::readwrite);
    if (!store) {
        spdlog::error("unable to open repo: {}", store.error());
        return 1;
    }

    //
    // check whether the label also exists
    //
    bool existing_label = false;
    {
        auto results = store->query(*label);
        // TODO check error on results
        existing_label = !results->empty();

        if (existing_label) {
            spdlog::error("image_add: a uenv already exists with the label {}",
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
        uenv_label hash_label{hash};
        auto results = store->query(hash_label);
        // TODO check error on results
        existing_hash = !results->empty();

        if (existing_hash) {
            spdlog::warn("a uenv with the same sha {} is already in the repo",
                         hash);
        }
    }

    const auto uenv_paths = store->uenv_paths(hash);

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
    uenv::uenv_date date{*util::file_creation_date(sqfs)};

    fs::create_directories(uenv_paths.store, ec);
    if (ec) {
        spdlog::error("unable to create path {}: {}", uenv_paths.store.string(),
                      ec.message());
        return 1;
    }

    //
    // copy the meta data into the repo
    //
    if (auto p = util::unsquashfs_tmp(sqfs, "meta")) {
        fs::copy_options options{};
        options |= fs::copy_options::recursive;
        fs::copy(p.value() / "meta", uenv_paths.meta, options, ec);
        if (ec) {
            spdlog::error("unable to copy meta data to {}: {}",
                          uenv_paths.meta.string(), ec.message());
            return 1;
        }
    }

    // copy or move the
    if (!args.move) {
        fs::copy_file(sqfs, uenv_paths.squashfs, ec);
        if (ec) {
            spdlog::error("unable to copy squashfs image {} to {}: {}",
                          sqfs.string(), uenv_paths.squashfs.string(),
                          ec.message());
            return 1;
        }
    } else {
        fs::rename(sqfs, uenv_paths.squashfs, ec);
        if (ec) {
            spdlog::error("unable to move squashfs image {} to {}: {}",
                          sqfs.string(), uenv_paths.squashfs.string(),
                          ec.message());
            fmt::println(
                "{}",
                help::item{help::block{
                    help::block::admonition::note,
                    fmt::format(
                        "check that the file {} is on the same filesystem as "
                        "the repository, and that you have write access to it.",
                        sqfs)}});
            return 1;
        }
    }

    //
    // add the uenv to the database
    //
    if (!date.validate()) {
        spdlog::error("the date {} is invalid", date);
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
        hash,
        hash.substr(0, 16),
    };
    if (auto result = store->add(r); !result) {
        spdlog::error("image_add: {}", result.error());
        return 1;
    }
    fmt::println("the uenv {} with sha {} was added to {}", r, hash,
                 store->path()->string());

    return 0;
}

int image_remove([[maybe_unused]] const image_remove_args& args,
                 [[maybe_unused]] const global_settings& settings) {
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

std::string image_remove_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Remove a uenv image from a repository." },
        help::linebreak{},
        help::block{xmpl, "by label"},
        help::block{code,   "uenv image remove prgenv-gnu/24.7:v1"},
        help::linebreak{},
        help::block{xmpl, "by label"},
        help::block{code,   "uenv image remove prgenv-gnu"},
        help::block{code,   "uenv image remove prgenv-gnu/24.7"},
        help::block{none, "should this delete all images that match?"},
        help::linebreak{},
        help::block{xmpl, "by sha"},
        help::block{code,   "uenv image remove abcd1234abcd1234abcd1234abcd1234"},
        help::linebreak{},
        help::block{xmpl, "by id"},
        help::block{code,   "uenv image remove abcd1234"},
        help::linebreak{},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
