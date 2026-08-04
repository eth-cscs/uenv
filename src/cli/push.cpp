// vim: ts=4 sts=4 sw=4 et

#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>

#include <barkeep/barkeep.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <oci/auth.h>
#include <oci/client.h>
#include <oci/manifest.h>
#include <oci/push.h>
#include <site/site.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/color.h>
#include <util/curl.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/signal.h>

#include "help.h"
#include "push.h"
#include "terminal.h"
#include "util.h"

namespace uenv {

std::string image_push_footer();

void image_push_args::add_cli(CLI::App& cli,
                              [[maybe_unused]] global_settings& settings) {
    auto* push_cli = cli.add_subcommand("push", "push a uenv to a registry");
    push_cli
        ->add_option("source", source,
                     "the local uenv to push, either name/version:tag, sha256, "
                     "id, or the path of a SquashFS file")
        ->required();
    push_cli
        ->add_option("dest", dest,
                     "the destination in the full "
                     "namespace::name/version:tag@system%uarch form")
        ->required();
    push_cli->add_option(
        "--token", token,
        "a path that contains a TOKEN file for accessing the registry");
    push_cli->add_option(
        "--username", username,
        "user name for the registry (by default $USER is used).");
    push_cli->add_flag("--force", force,
                       "overwrite the destination if it exists");
    push_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_push; });

    push_cli->footer(image_push_footer);
}

int image_push([[maybe_unused]] const image_push_args& args,
               [[maybe_unused]] const global_settings& settings) {
    if (!settings.config.registry) {
        term::error("registry is not configured: add a [registry] section to "
                    "your uenv configuration file");
        return 1;
    }
    const auto& registry_cfg = *settings.config.registry;

    std::optional<oci::credentials> credentials;
    if (auto c = resolve_registry_credentials(settings.calling_environment,
                                              registry_cfg.url, args.username,
                                              args.token)) {
        credentials = *c;
    } else {
        term::error("{}", c.error());
        return 1;
    }

    // parse and validate the destination (i.e. the label in the registry)
    uenv_nslabel dst_label{};
    if (const auto parse = parse_uenv_nslabel(args.dest)) {
        dst_label = *parse;
    } else {
        term::error("invalid destination: {}", parse.error().message());
        return 1;
    }
    if (!dst_label.nspace || !dst_label.label.fully_qualified()) {
        term::error("the destination uenv {} must be fully qualified, e.g. "
                    "'deploy::name/version:tag%system%gh200'",
                    args.dest);
        return 1;
    }
    spdlog::debug("destination label {}::{}", dst_label.nspace,
                  dst_label.label);

    const auto nspace = dst_label.nspace.value();
    auto registry = site::registry_listing(registry_cfg.listing_url, nspace);
    if (!registry) {
        term::error("unable to get a listing of the uenv", registry.error());
        return 1;
    }

    // check whether an image that matches the target label is already
    // in the registry.
    const auto remote_matches = registry->query(dst_label.label);
    if (!remote_matches) {
        term::error("invalid search term: {}", registry.error());
        return 1;
    }
    if (!remote_matches->empty() && !args.force) {
        using enum help::block::admonition;
        term::error(
            "a uenv that matches '{}' is already in the registry\n\n{}",
            args.dest,
            help::block(info,
                        "use the --force flag if you want to overwrite it "));
        return 1;
    } else if (!remote_matches->empty() && args.force) {
        spdlog::info("{} already exists and will be overwritten", args.dest);
        term::msg("the destination already exists and will be overwritten");
    }

    // validate the source squashfs file. This hashes the whole image, which
    // takes minutes for a multi-GB uenv, so show a progress bar for it. The bar
    // is created on the first callback, once the image size is known.
    std::unique_ptr<uenv::transfer_bar> prepare_bar;
    auto sqfs = uenv::validate_squashfs_image(
        args.source,
        [&prepare_bar, &args](std::uint64_t done, std::uint64_t total) {
            if (!prepare_bar) {
                prepare_bar = uenv::make_transfer_bar(
                    total, fmt::format("validating {}",
                                       std::filesystem::path{args.source}
                                           .filename()
                                           .string()));
            }
            prepare_bar->update(done);
        });
    // stop the bar before anything else is printed, so that an error message
    // does not land on the bar's line.
    if (prepare_bar) {
        prepare_bar->finish();
    }
    if (!sqfs) {
        term::error("invalid squashfs file {}: {}", args.source, sqfs.error());
        return 1;
    }
    spdlog::info("image_push: squashfs {}", sqfs.value());

    // split the configured registry "host/prefix" into a base URL + prefix and
    // build the OCI repository path (the same address oras used).
    const auto loc = oci::split_registry(registry_cfg.url);
    const auto& L = dst_label.label;
    const auto repository = oci::repository_path(loc.prefix, nspace, *L.system,
                                                 *L.uarch, *L.name, *L.version);
    spdlog::debug("oci push: registry={} repository={}", loc.base, repository);

    auto client = oci::client::create(loc.base, repository, credentials);
    if (!client) {
        term::error("unable to connect to the registry:\n{}", client.error());
        return 1;
    }

    namespace bk = barkeep;

    auto push_tag = oci::tag::parse(*L.tag);
    if (!push_tag) {
        term::error("invalid tag '{}': {}", *L.tag, push_tag.error().message());
        return 1;
    }

    // Push the SquashFS image. Ctrl-C is left with its default behaviour: the
    // upload session is not committed until the manifest is PUT, so an
    // interrupt leaves nothing referencing a partial blob.
    std::optional<oci::digest> image_digest;
    {
        std::error_code ec;
        auto size_byte = std::filesystem::file_size(sqfs->sqfs, ec);
        auto bar = uenv::make_transfer_bar(
            ec ? 0u : size_byte,
            fmt::format("pushing    {}", sqfs->sqfs.filename().string()));

        auto progress = [&bar](std::uint64_t now, std::uint64_t) {
            bar->update(now);
        };
        // the image was hashed by validate_squashfs_image; pass that digest in
        // rather than reading the whole file a second time.
        auto push_result = oci::push_squashfs(
            *client, sqfs->sqfs, oci::reference::tag(*push_tag),
            oci::digest::sha256(sqfs->hash), progress);
        // fill the bar on success, which also covers the case where the
        // registry already had the blob and no upload happened.
        if (push_result) {
            bar->finish();
        } else {
            bar->stop();
        }
        if (!push_result) {
            term::error("unable to push uenv.\n{}", push_result.error());
            return 1;
        }
        image_digest = *push_result;
    }
    spdlog::info("image_push: pushed image manifest {}", *image_digest);

    // Check for metadata and attach it if available.
    if (sqfs->meta) {
        spdlog::info("image_push: pushing metadata from {}",
                     sqfs->meta.value().string());
        auto spinner = bk::Animation({
            .message = "pushing    metadata",
            .style = bk::Ellipsis,
            .no_tty = !isatty(fileno(stdout)),
        });
        auto meta_result =
            oci::attach(*client, oci::reference::digest(*image_digest),
                        oci::artifact_type_meta, *sqfs->meta);
        spinner->done();
        if (!meta_result) {
            spdlog::warn("unable to push metadata.\n{}", meta_result.error());
            term::warn("unable to push metadata.\n{}", meta_result.error());
            // Continue even if metadata push fails.
        } else {
            spdlog::info("successfully pushed metadata");
        }
    }

    term::msg("successfully pushed {}", args.source);
    term::msg("to {}", args.dest);

    return 0;
} // namespace uenv

std::string image_push_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Push a uenv to a registry." },
        help::linebreak{},
        help::linebreak{},
        help::block{xmpl, "push a uenv from your local repository"},
        help::block{code,   "uenv image push prgenv-gnu/24.11:v3 prgenv-gnu/24.11:v3%gh200@daint"},
        help::linebreak{},
        help::block{xmpl, "push a uenv from a SquashFS file on the local filesystem"},
        help::block{code,   "uenv image push ./store.squashfs prgenv-gnu/24.11:v3%gh200@daint"},
        help::linebreak{},
        help::block{xmpl, "overwrite an existing uenv in the registry"},
        help::block{code,   "uenv image push --force ./store.squashfs prgenv-gnu/24.11:v3%gh200@daint"},
        help::linebreak{},
        help::block{xmpl, "use a token for the registry"},
        help::block{code,   "uenv image push --token=/opt/cscs/uenv/tokens/vasp6 \\"},
        help::block{code,   "                ./store.squashfs prgenv-gnu/24.11:v3%gh200@daint"},
        help::block{note, "credentials are resolved in order: --token, then the" },
        help::block{none, "uenv token store $XDG_CONFIG_HOME/uenv/tokens/<registry>," },
        help::block{none, "then ~/.docker/config.json." },
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
