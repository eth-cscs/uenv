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

    //
    // the repos and uenv_arg are used to set the UENV and UENV_REPO environment
    // variables in the patch() function.
    //

    // the list of repositories that were used to look up environments
    repo_list repos;

    // the argument passed to --uenv/uenv run/uenv start to look up
    std::string uenv_arg;

    // the environment variable patch
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
    // path to manifest.json if looked up from repo
    std::optional<std::filesystem::path> manifest_path;
    // meta data loaded from meta_path/env.json
    std::optional<uenv::meta> meta;
    // the repo this image was found in (nullopt for squashfs file path inputs)
    std::optional<repo_description> repo;
};

/// A resolved uenv paired with an optional CLI-provided mount override.
/// Passed to concretise_env after resolve_uenv has been called by the caller.
struct resolved_uenv {
    uenv_info info;
    // mount point explicitly provided on the CLI (overrides metadata mount)
    std::optional<std::string> mount_override;
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

util::expected<uenv_info, std::string>
resolve_uenv(const uenv_description& desc, const repo_list& repos);

/// Parse a raw CLI uenv description string, apply system filtering, and
/// resolve each entry against the provided repos. Returns the vector of
/// resolved_uenv ready to pass to concretise_env.
util::expected<std::vector<resolved_uenv>, std::string>
resolve_uenv_args(const std::string& uenv_description, const repo_list& repos,
                  std::optional<std::string> system_name = std::nullopt);

util::expected<env, std::string>
concretise_env(const std::vector<resolved_uenv>& uenvs,
               const std::optional<std::string>& view_args,
               const std::string& uenv_description, const repo_list& repos,
               bool use_default_views);

envvars::state generate_environment(const env&, const envvars::state&,
                                    std::optional<std::string> = std::nullopt);

// returns true iff in a running uenv session
bool in_uenv_session(const envvars::state&);

} // namespace uenv
