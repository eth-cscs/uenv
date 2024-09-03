#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <uenv/uenv.h>
#include <util/expected.h>

namespace uenv {

// PIMPL forward declaration
struct repository;

/// get the default location for the user's repository.
/// - use the environment variable UENV_REPO_PATH if it is set
/// - use $SCRATCH/.uenv-images if $SCRATCH is set
/// - use $HOME/.uenv/images
util::expected<std::optional<std::string>, std::string> default_repo_path();

util::expected<std::filesystem::path, std::string>
validate_repo_path(const std::string& path, bool is_absolute = true,
                   bool exists = true);

util::expected<repository, std::string>
open_repository(const std::filesystem::path&);

struct repository_impl;
struct repository {
  private:
    std::unique_ptr<repository_impl> impl_;

  public:
    repository(repository&&);
    repository(std::unique_ptr<repository_impl>);

    // copying a repository is not permitted
    repository() = delete;
    repository(const repository&) = delete;

    const std::filesystem::path& path() const;
    const std::filesystem::path& db_path() const;

    util::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label& label);

    ~repository();

    friend util::expected<repository, std::string>
    open_repository(const std::filesystem::path&);
};

} // namespace uenv
