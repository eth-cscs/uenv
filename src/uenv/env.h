#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <util/envvars.h>
#include <util/expected.h>

#include <uenv/meta.h>
#include <uenv/repository.h>
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

/// stores information about uenv in a repo
/// used to hold information returned from querying a repo
class resolved_record_set {
    std::vector<uenv_info> records_;
    repo_description repo_;

  public:
    resolved_record_set() = delete;
    resolved_record_set(repo_description repo, std::vector<uenv_info> r);

    using iterator = std::vector<uenv_info>::iterator;
    using const_iterator = std::vector<uenv_info>::const_iterator;

    const repo_description& repo() const;
    record_set as_record_set() const;

    bool empty() const;

    std::size_t size() const;

    // return true if there is one or more record, and they all have
    // the same sha. Otherwise returns false if no records, or if there are at
    // least 2 records with different sha
    bool unique_sha() const;

    // iterator access to the underlying records
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const;
    const_iterator cend() const;
};

/// Resolve a uenv description to get squashfs path and metadata.
/// Takes either a label (to look up in repo) or a file path.
/// Does NOT handle mount point resolution from CLI - returns only the mount
/// from metadata.
/*
util::expected<uenv_info, std::string>
resolve_uenv(const uenv_description& desc,
             std::optional<std::filesystem::path> repo_arg,
             const envvars::state& calling_env);
*/

util::expected<uenv_info, std::string>
resolve_uenv(const uenv_description& desc,
             const std::vector<repo_description>& repos);

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
