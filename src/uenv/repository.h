#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <uenv/uenv.h>
#include <util/expected.h>

namespace uenv {

/// get the default location for the user's repository.
/// - use the environment variable UENV_REPO_PATH if it is set
/// - use $SCRATCH/.uenv-images if $SCRATCH is set
/// - use $HOME/.uenv/images
util::expected<std::optional<std::string>, std::string> default_repo_path();

util::expected<std::filesystem::path, std::string>
validate_repo_path(const std::string& path, bool is_absolute = true,
                   bool exists = true);

enum class repo_state { readonly, readwrite, no_exist, invalid };
repo_state validate_repository(const std::filesystem::path& repo_path);

enum class repo_mode : std::uint8_t { readonly, readwrite };

struct repository_impl;
struct repository {
  private:
    std::unique_ptr<repository_impl> impl_;

  public:
    using enum repo_mode;
    repository() = delete;

    repository(repository&&);
    repository(std::unique_ptr<repository_impl>);

    // copying a repository is not permitted
    repository(const repository&) = delete;

    // returns nullopt if the data base is in memory
    std::optional<std::filesystem::path> path() const;

    util::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label& label);

    // return true if the repository is readonly
    bool is_readonly() const;

    // return true if the repository is in memory
    bool is_in_memory() const;

    ~repository();

    friend util::expected<repository, std::string>
    open_repository(const std::filesystem::path&, repo_mode mode);
};

util::expected<repository, std::string>
open_repository(const std::filesystem::path&,
                repo_mode mode = repo_mode::readonly);

util::expected<repository, std::string>
create_repository(const std::filesystem::path& repo_path);

util::expected<repository, std::string> create_repository();

} // namespace uenv
