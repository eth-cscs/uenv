// vim: ts=4 sts=4 sw=4 et

#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <oci/auth.h>
#include <oci/client.h>
#include <oci/digest.h>
#include <oci/manifest.h>
#include <oci/pull.h>
#include <oci/reference.h>
#include <oci/types.h>
#include <site/site.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/pull.h>
#include <uenv/repository.h>
#include <uenv/settings.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/signal.h>

namespace uenv {

namespace {

// Resolve registry credentials from the calling environment (uenv token
// store, docker config.json). Returns std::nullopt for anonymous access.
util::expected<std::optional<oci::credentials>, std::string>
resolve_creds_from_env(const uenv::registry_config& registry_cfg,
                       const envvars::state& env) {
    namespace fs = std::filesystem;

    const std::string host = registry_cfg.url.host_port();

    oci::credential_sources sources;
    sources.username = envvars::user_name(env);

    if (auto config_home = envvars::xdg_dir(env, "XDG_CONFIG_HOME", ".config")) {
        sources.uenv_token_dir = *config_home / "uenv" / "tokens";
    }

    if (auto dcfg = env.get("DOCKER_CONFIG")) {
        sources.docker_config = fs::path{*dcfg} / "config.json";
    } else if (auto home = env.get("HOME")) {
        sources.docker_config = fs::path{*home} / ".docker" / "config.json";
    }

    auto creds = oci::resolve_credentials(host, sources);
    if (!creds && creds.error() == oci::username_required_error) {
        return util::unexpected{
            "a token was found, but uenv could not determine your username: "
            "pass it with --username.\nThe username is taken from --username, "
            "or from $USER, which may be unset in a sanitised environment."};
    }
    return creds;
}

} // namespace

util::expected<pull_result, std::string>
pull_uenv(const uenv_label& label, const std::string& nspace,
          const configuration& config, const envvars::state& calling_env,
          const pull_options& opts) {
    namespace fs = std::filesystem;

    if (!config.registry) {
        return util::unexpected{
            "registry is not configured: add a [registry] section to your "
            "uenv configuration file"};
    }
    const auto& registry_cfg = *config.registry;

    // Resolve credentials: explicit if provided, otherwise from the
    // calling environment.
    std::optional<oci::credentials> credentials = opts.creds;
    if (!credentials) {
        auto c = resolve_creds_from_env(registry_cfg, calling_env);
        if (!c) {
            return util::unexpected{c.error()};
        }
        credentials = *c;
    }

    spdlog::info("pull_uenv: {}::{}", nspace, label);

    // Fetch the registry listing as an in-memory repository and query
    // it for matching records.
    auto registry = site::registry_listing(registry_cfg.listing_url, nspace,
                                           user_cache_path(calling_env));
    if (!registry) {
        return util::unexpected{fmt::format(
            "unable to get a listing of the uenv: {}", registry.error())};
    }

    const auto remote_matches = registry->query(label);
    if (!remote_matches) {
        return util::unexpected{
            fmt::format("invalid search term: {}", remote_matches.error())};
    }
    if (remote_matches->empty()) {
        return util::unexpected{
            fmt::format("no uenv found that matches '{}'", label)};
    }
    if (!remote_matches->unique_sha()) {
        std::string errmsg = fmt::format(
            "more than one uenv found that matches '{}':\n", label);
        errmsg += format_record_set_table(*remote_matches);
        return util::unexpected{errmsg};
    }

    // Pick a record to use for pulling.
    const auto record = *(remote_matches->begin());
    spdlog::info("pulling {} {}", record.sha, record);

    // Find/create and open the default repository.
    auto store = uenv::concretise_user_repo(config);
    if (!store) {
        return util::unexpected{
            fmt::format("unable to open repo: {}", store.error())};
    }

    auto paths = store->uenv_paths(record.sha);

    // Acquire a file lock so that only one process can try to pull an
    // image.
    auto lock = util::make_file_lock(paths.store.string() + ".lock");

    bool meta_exists = fs::exists(paths.meta);
    bool sqfs_exists = fs::exists(paths.squashfs);

    auto in_repo = [&store](uenv_label l) -> bool {
        return !(store->query(l)->empty());
    };
    const bool sha_in_repo = in_repo({.name = record.sha.string()});
    const bool label_in_repo = in_repo({.name = record.name,
                                        .version = record.version,
                                        .tag = record.tag,
                                        .system = record.system,
                                        .uarch = record.uarch});

    spdlog::debug("sha   in repo: {}", sha_in_repo);
    spdlog::debug("label in repo: {}", label_in_repo);

    const bool pull_sqfs = !opts.only_meta && (opts.force || !sqfs_exists);
    const bool pull_meta = opts.force || !meta_exists;
    spdlog::debug("pull meta: {}", pull_meta);
    spdlog::debug("pull sqfs: {}", pull_sqfs);

    bool downloaded = false;

    if (pull_sqfs || pull_meta) {
        if (opts.callbacks.on_pull_start) {
            opts.callbacks.on_pull_start(record);
        }

        // Split the configured registry "host/prefix" (e.g.
        // "jfrog.svc.cscs.ch/uenv") into a base URL and the repository
        // prefix, then build the OCI repository name the same way the
        // oras address was formed:
        // <prefix>/<nspace>/<system>/<uarch>/<name>/<version>.
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
            if (opts.callbacks.on_pull_end) {
                opts.callbacks.on_pull_end(false);
            }
            return util::unexpected{fmt::format(
                "unable to connect to the registry:\n{}", client.error())};
        }

        // Identify the image by its manifest digest (record.sha).
        const auto image_digest = oci::digest::sha256(record.sha);
        const auto manifest_ref = oci::reference::digest(image_digest);

        try {
            // The image manifest is needed for the squashfs layer; fetch
            // and parse once.
            auto response = client->get_manifest(manifest_ref);
            if (!response) {
                if (opts.callbacks.on_pull_end) {
                    opts.callbacks.on_pull_end(false);
                }
                return util::unexpected{fmt::format(
                    "unable to fetch the image manifest:\n{}",
                    response.error())};
            }
            auto manifest = oci::parse_manifest(response->body);
            if (!manifest) {
                if (opts.callbacks.on_pull_end) {
                    opts.callbacks.on_pull_end(false);
                }
                return util::unexpected{fmt::format(
                    "unable to parse the image manifest:\n{}",
                    manifest.error())};
            }

            if (pull_meta) {
                auto found =
                    oci::pull_meta(*client, image_digest, paths.store);
                if (!found) {
                    if (opts.callbacks.on_pull_end) {
                        opts.callbacks.on_pull_end(false);
                    }
                    return util::unexpected{fmt::format(
                        "unable to pull meta data.\n{}", found.error())};
                }
                if (!*found) {
                    // No metadata attached. Error if the user explicitly
                    // wanted it; otherwise warn and continue to the
                    // squashfs.
                    if (!pull_sqfs) {
                        if (opts.callbacks.on_pull_end) {
                            opts.callbacks.on_pull_end(false);
                        }
                        return util::unexpected{
                            "uenv exists in registry but has no attached "
                            "metadata"};
                    }
                    spdlog::warn(
                        "uenv exists in registry but has no attached "
                        "metadata");
                }
            }

            if (pull_sqfs) {
                // util::signal_raised() consumes (resets) the flag, so it
                // must be checked exactly once. latch the result here in
                // the abort predicate; the post-download check then reads
                // the latch rather than calling signal_raised() again
                // (which would see false and skip the cleanup, leaving a
                // partial download behind).
                bool aborted = false;
                util::set_signal_catcher();
                auto result = oci::pull_squashfs(
                    *client, *manifest, paths.store, opts.callbacks.progress,
                    [&aborted]() {
                        aborted = aborted || util::signal_raised();
                        return aborted;
                    });

                if (result) {
                    downloaded = true;
                    if (opts.callbacks.on_pull_end) {
                        opts.callbacks.on_pull_end(true);
                    }
                } else {
                    if (opts.callbacks.on_pull_end) {
                        opts.callbacks.on_pull_end(false);
                    }
                    // A Ctrl-C during the download aborts the transfer;
                    // surface it as a signal so the cleanup below runs.
                    if (aborted) {
                        throw util::signal_exception(
                            util::last_signal_raised());
                    }
                    return util::unexpected{fmt::format(
                        "unable to pull uenv.\n{}", result.error())};
                }
            } else if (opts.callbacks.on_pull_end) {
                opts.callbacks.on_pull_end(true);
            }

            // Persist the manifest alongside the image, so that the hash
            // this image is stored under locally (record.sha, the
            // manifest digest reported by the registry) is explained by a
            // manifest on disk, same as a locally-added image.
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
    }

    // Add the label to the repo, even if there was no download. Download
    // may have been skipped if a squashfs with the same sha has been
    // downloaded, and this download uses a different label.
    std::vector<uenv_record> added;
    for (auto& r : *remote_matches) {
        bool exists = in_repo({.name = r.name,
                               .version = r.version,
                               .tag = r.tag,
                               .system = r.system,
                               .uarch = r.uarch});
        if (!exists) {
            spdlog::debug("adding {} to repo", r);
            store->add(r);
            added.push_back(r);
        }
    }

    return pull_result{
        .record = record,
        .downloaded = downloaded,
        .added = std::move(added),
    };
}

util::expected<std::vector<resolved_uenv>, std::string>
resolve_and_ensure_uenvs(const std::string& uenv_description,
                         const configuration& config,
                         const envvars::state& calling_env,
                         bool auto_pull,
                         const pull_callbacks& callbacks) {
    // When auto_pull is false, behave identically to resolve_uenv_args.
    if (!auto_pull) {
        return resolve_uenv_args(uenv_description, config.repos,
                                 config.system_name);
    }

    const auto descs = parse_uenv_args(uenv_description);
    if (!descs) {
        return util::unexpected(fmt::format("invalid uenv description: {}",
                                            descs.error().message()));
    }

    std::vector<resolved_uenv> result;
    for (auto desc : descs.value()) {
        desc = apply_system(desc, config.system_name);
        auto info = resolve_uenv(desc, config.repos);
        if (!info) {
            // Only auto-pull labels, not file paths.
            auto label_opt = desc.label();
            if (!label_opt || !config.registry) {
                return util::unexpected(info.error());
            }
            const auto& nspace = config.registry->default_namespace;
            auto pr = pull_uenv(*label_opt, nspace, config, calling_env,
                                {.callbacks = callbacks});
            if (!pr) {
                return util::unexpected(fmt::format(
                    "image '{}' not found locally and could not be "
                    "pulled from registry:\n  {}",
                    *label_opt, pr.error()));
            }
            // Re-resolve now that the image is local.
            info = resolve_uenv(desc, config.repos);
            if (!info) {
                return util::unexpected(fmt::format(
                    "image '{}' was pulled but could not be resolved: {}",
                    *label_opt, info.error()));
            }
        }
        result.push_back(resolved_uenv{info.value(), desc.mount()});
    }
    return result;
}

} // namespace uenv
