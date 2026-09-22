// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <oci/auth.h>
#include <uenv/env.h>
#include <uenv/settings.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

// Callbacks for the download phase, so the CLI (progress bar) and the
// Slurm plugin (slurm_verbose) each control their own output. All
// optional; an unset callback is simply not called.
struct pull_callbacks {
    // Called once the registry listing returns a unique match, before
    // the download starts (only when a download is actually needed).
    std::function<void(const uenv_record&)> on_pull_start;
    // Called with (bytes_downloaded, total) during the squashfs
    // download. Passed through to oci::pull_squashfs.
    std::function<void(std::uint64_t, std::uint64_t)> progress;
    // Called after the download completes (true) or fails (false). Not
    // called when the download was skipped because the image was
    // already present.
    std::function<void(bool success)> on_pull_end;
};

// Optional parameters for pull_uenv.
struct pull_options {
    // re-download even if the squashfs/meta are already present locally
    bool force = false;
    // only download meta, not the squashfs image
    bool only_meta = false;
    // if set, use these credentials; otherwise resolve from calling_env
    // (uenv token store, docker config.json)
    std::optional<oci::credentials> creds;
    // callbacks for download progress and messaging
    pull_callbacks callbacks;
};

// The outcome of a pull: which record was pulled from the registry,
// whether the squashfs was actually downloaded (false if already
// present and !force), and which records were added to the local
// repo's index.db.
struct pull_result {
    uenv_record record;
    bool downloaded = false;
    std::vector<uenv_record> added;
};

// Download a uenv image (by label) from the registry into the local
// repository. Searches the registry listing for `nspace` (defaults to
// config.registry->default_namespace), finds a unique match for
// `label`, and downloads the squashfs + meta (unless only_meta or
// already present and !force). The matching labels are registered in
// the local repo's index.db after the download (same as
// `uenv image pull`).
//
// When opts.creds is set, uses them directly; otherwise resolves
// credentials from calling_env (uenv token store, docker config.json).
//
// Returns an error string suitable for the user on failure.
util::expected<pull_result, std::string>
pull_uenv(const uenv_label& label, const std::string& nspace,
          const configuration& config, const envvars::state& calling_env,
          const pull_options& opts = {});

// Resolve uenv descriptions to local uenvs, auto-pulling from the
// registry when `auto_pull` is true and a label is not found locally.
// When `auto_pull` is false, behaves identically to
// `resolve_uenv_args`.
//
// File-path descriptions are never auto-pulled. If the registry is not
// configured, the original resolution error is returned so the caller
// can display it with the existing hint.
util::expected<std::vector<resolved_uenv>, std::string>
resolve_and_ensure_uenvs(const std::string& uenv_description,
                         const configuration& config,
                         const envvars::state& calling_env,
                         bool auto_pull = false,
                         const pull_callbacks& callbacks = {});

} // namespace uenv
