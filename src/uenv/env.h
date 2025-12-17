#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <util/envvars.h>
#include <util/expected.h>

#include <uenv/meta.h>
#include <uenv/uenv.h>
#include <uenv/view.h>

namespace uenv {

struct env {
    std::unordered_map<std::string, concrete_uenv> uenvs;

    // the order of views matters: views are initialised in order
    std::vector<qualified_view_description> views;

    envvars::patch patch() const;
};

/// Information about a uenv resolved from a label or file path.
/// Does not include mount point resolution from CLI (only from metadata).
struct uenv_info {
    // path to the squashfs file
    std::filesystem::path sqfs_path;
    // sha256 digest if looked up from repo
    std::optional<uenv_record> record;
    // path to meta directory if found
    std::optional<std::filesystem::path> meta_path;
    // meta data loaded from meta_path/env.json
    std::optional<uenv::meta> meta;
};

/// Resolve a uenv description to get squashfs path and metadata.
/// Takes either a label (to look up in repo) or a file path.
/// Does NOT handle mount point resolution from CLI - returns only the mount
/// from metadata.
util::expected<uenv_info, std::string>
resolve_uenv_info(const uenv_description& desc,
                  std::optional<std::filesystem::path> repo_arg,
                  const envvars::state& calling_env);

util::expected<env, std::string>
concretise_env(const std::string& uenv_args,
               std::optional<std::string> view_args,
               std::optional<std::filesystem::path> repo_arg,
               const envvars::state& calling_env);

envvars::state generate_environment(const env&, const envvars::state&,
                                    std::optional<std::string> = std::nullopt);

void patch_slurm_environment(const env&, const envvars::state&);

// returns true iff in a running uenv session
bool in_uenv_session(const envvars::state&);

} // namespace uenv
