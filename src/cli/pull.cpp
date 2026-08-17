// vim: ts=4 sts=4 sw=4 et

#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <oci/client.h>
#include <oci/pull.h>
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
#include "pull.h"
#include "terminal.h"
#include "util.h"

namespace uenv {

std::string image_pull_footer();

void image_pull_args::add_cli(CLI::App& cli,
                              [[maybe_unused]] global_settings& settings) {
    auto* pull_cli =
        cli.add_subcommand("pull", "download a uenv from a registry");
    pull_cli
        ->add_option("uenv", uenv_description,
                     "the uenv to pull, either name/version:tag, sha256 or id")
        ->required();
    pull_cli->add_option(
        "--token", token,
        "a path that contains a TOKEN file for accessing restricted uenv");
    pull_cli->add_option("--username", username,
                         "user name for accessing restricted uenv.");
    pull_cli->add_flag("--only-meta", only_meta, "only download meta data");
    pull_cli->add_flag("--force", force,
                       "download and overwrite existing images");
    pull_cli->add_flag("--build", build,
                       "invalid: replaced with 'build::' prefix on uenv label");
    pull_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_pull; });

    pull_cli->footer(image_pull_footer);
}

int image_pull(const image_pull_args& args, const global_settings& settings) {
    namespace fs = std::filesystem;

    if (args.build) {
        term::error(
            "the --build flag has been removed.\nSpecify the build namespace "
            "as part of the uenv description, e.g.\n{}",
            color::yellow(fmt::format("uenv image pull build::{}",
                                      args.uenv_description)));
        return 1;
    }

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

    // pull the search term that was provided by the user
    uenv_label label{};
    std::string nspace{registry_cfg.default_namespace};
    if (const auto parse = parse_uenv_nslabel(args.uenv_description)) {
        label = parse->label;
        if (parse->nspace) {
            nspace = *parse->nspace;
        }
    } else {
        term::error("invalid search term: {}", parse.error().message());
        return 1;
    }

    label = apply_system(label, settings.config.system_name);
    if (!label.name) {
        term::error(
            "the uenv description '{}' must specify the name of the uenv",
            args.uenv_description);
        return 1;
    }

    spdlog::info("image_pull: {}::{}", nspace, label);

    auto registry = site::registry_listing(registry_cfg.listing_url, nspace);
    if (!registry) {
        term::error("unable to get a listing of the uenv", registry.error());
        return 1;
    }

    // search db for matching records
    const auto remote_matches = registry->query(label);
    if (!remote_matches) {
        term::error("invalid search term: {}", registry.error());
        return 1;
    }
    // check that there is one record with a unique sha
    if (remote_matches->empty()) {
        using enum help::block::admonition;
        term::error("no uenv found that matches '{}'\n\n{}",
                    args.uenv_description,
                    help::block(info, "try searching for the uenv to pull "
                                      "first using 'uenv image find'"));
        return 1;
    } else if (!remote_matches->unique_sha()) {
        std::string errmsg =
            fmt::format("more than one uenv found that matches '{}':\n",
                        args.uenv_description);
        errmsg += format_record_set_table(*remote_matches);
        term::error("{}", errmsg);
        return 1;
    }

    // pick a record to use for pulling
    const auto record = *(remote_matches->begin());
    spdlog::info("pulling {} {}", record.sha, record);

    // find/create and open the default repository
    auto store = uenv::concretise_user_repo(settings.config);
    if (!store) {
        term::error("unable to open repo: {}", store.error());
        return 1;
    }

    auto paths = store->uenv_paths(record.sha);

    // acquire a file lock so that only one process can try to pull an image.
    // TODO: how do we handle the case where we have many processes waiting, and
    // there is a failure (e.g. file system problem), that causes the processes
    // to attempt the pull one-after-the-other
    auto lock = util::make_file_lock(paths.store.string() + ".lock");

    bool meta_exists = fs::exists(paths.meta);
    bool sqfs_exists = fs::exists(paths.squashfs);

    auto in_repo = [&store](uenv_label label) -> bool {
        return !(store->query(label)->empty());
    };
    const bool sha_in_repo = in_repo({.name = record.sha.string()});
    const bool label_in_repo = in_repo({.name = record.name,
                                        .version = record.version,
                                        .tag = record.tag,
                                        .system = record.system,
                                        .uarch = record.uarch});

    spdlog::debug("sha   in repo: {}", sha_in_repo);
    spdlog::debug("label in repo: {}", label_in_repo);

    const bool pull_sqfs = !args.only_meta && (args.force || !sqfs_exists);
    const bool pull_meta = args.force || !meta_exists;
    spdlog::debug("pull meta: {}", pull_meta);
    spdlog::debug("pull sqfs: {}", pull_sqfs);

    if (pull_sqfs || pull_meta) {
        // split the configured registry "host/prefix" (e.g.
        // "jfrog.svc.cscs.ch/uenv") into a base URL and the repository prefix,
        // then build the OCI repository name the same way the oras address was
        // formed: <prefix>/<nspace>/<system>/<uarch>/<name>/<version>.
        const auto loc = oci::split_registry(registry_cfg.url);
        const auto registry_base = loc.base;
        const std::string repository =
            oci::repository_path(loc.prefix, nspace, record.system,
                                 record.uarch, record.name, record.version);

        spdlog::debug("oci pull: registry={} repository={}", registry_base,
                      repository);

        auto client =
            oci::client::create(registry_base, repository, credentials);
        if (!client) {
            term::error("unable to connect to the registry:\n{}",
                        client.error());
            return 1;
        }

        // identify the image by its manifest digest (record.sha).
        const auto image_digest = oci::digest::sha256(record.sha);
        const auto manifest_ref = oci::reference::digest(image_digest);

        try {
            // the image manifest is needed for the squashfs layer; fetch +
            // parse once.
            auto response = client->get_manifest(manifest_ref);
            if (!response) {
                term::error("unable to fetch the image manifest:\n{}",
                            response.error());
                return 1;
            }
            auto manifest = oci::parse_manifest(response->body);
            if (!manifest) {
                term::error("unable to parse the image manifest:\n{}",
                            manifest.error());
                return 1;
            }

            if (pull_meta) {
                auto found = oci::pull_meta(*client, image_digest, paths.store);
                if (!found) {
                    term::error("unable to pull meta data.\n{}", found.error());
                    return 1;
                }
                if (!*found) {
                    // No metadata attached. Error if the user explicitly wanted
                    // it; otherwise warn and continue to the squashfs.
                    if (!pull_sqfs) {
                        term::error("uenv exists in registry but has no "
                                    "attached metadata");
                        return 1;
                    }
                    term::warn(
                        "uenv exists in registry but has no attached metadata");
                }
            }

            if (pull_sqfs) {
                auto bar = uenv::make_transfer_bar(
                    record.size_byte,
                    fmt::format("pulling {}", record.id.string()));

                auto progress = [&bar](std::uint64_t now, std::uint64_t) {
                    bar->update(now);
                };
                // util::signal_raised() consumes (resets) the flag, so it must
                // be checked exactly once. latch the result here in the abort
                // predicate; the post-download check then reads the latch
                // rather than calling signal_raised() again (which would see
                // false and skip the cleanup, leaving a partial download
                // behind).
                bool aborted = false;
                util::set_signal_catcher();
                auto result = oci::pull_squashfs(
                    *client, *manifest, paths.store, progress, [&aborted]() {
                        aborted = aborted || util::signal_raised();
                        return aborted;
                    });
                // fill the bar when the download succeeded.
                if (result) {
                    bar->finish();
                } else {
                    bar->stop();
                }

                if (!result) {
                    // a Ctrl-C during the download aborts the transfer; surface
                    // it as a signal so the cleanup below runs.
                    if (aborted) {
                        throw util::signal_exception(
                            util::last_signal_raised());
                    }
                    term::error("unable to pull uenv.\n{}", result.error());
                    return 1;
                }
            }

            // persist the manifest alongside the image, so that the hash
            // this image is stored under locally (record.sha, the manifest
            // digest reported by the registry) is explained by a manifest on
            // disk, same as a locally-added image.
            {
                std::ofstream mfid(paths.manifest);
                mfid << response->body;
                if (!mfid) {
                    spdlog::warn("unable to write manifest to {}",
                                 paths.manifest.string());
                }
            }
        } catch (util::signal_exception& e) {
            spdlog::info("cleaning up after interrupted download");
            spdlog::debug("removing record {}", record);
            store->remove(record.sha);
            spdlog::debug("deleting path {}", paths.store);
            std::filesystem::remove_all(paths.store);
            // reraise the signal
            raise(e.signal);
        }
    } else {
        term::msg("id={} already exists in the repository, skipping pull.",
                  record.id.string());
    }

    // add the label to the repo, even if there was no download.
    // download may have been skipped if a squashfs with the same sha has
    // been downloaded, and this download uses a different label.
    for (auto& r : *remote_matches) {
        bool exists = in_repo({.name = r.name,
                               .version = r.version,
                               .tag = r.tag,
                               .system = r.system,
                               .uarch = r.uarch});
        if (!exists) {
            term::msg("updating {}", r);
            store->add(r);
        }
    }

    return 0;
}

std::string image_pull_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Download a uenv from a registry." },
        help::linebreak{},
        help::linebreak{},
        help::block{xmpl, "pull a uenv"},
        help::block{code,   "uenv image pull prgenv-gnu"},
        help::block{code,   "uenv image pull prgenv-gnu/24.11:v1@todi"},
        help::linebreak{},
        help::block{xmpl, "use a token for the registry"},
        help::block{code,   "uenv image pull --token=/opt/cscs/uenv/tokens/vasp6 vasp/6.4.2:v1"},
        help::block{note, "this is only required when accessing uenv that require special" },
        help::block{none, "permission or a license to access." },
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
