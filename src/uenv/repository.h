#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <uenv/uenv.h>
#include <util/expected.h>

namespace uenv {

class record_set {
    std::vector<uenv_record> records_;

  public:
    record_set() = default;
    record_set(const std::vector<uenv_record> r) : records_(std::move(r)) {
    }
    using iterator = std::vector<uenv_record>::iterator;
    using const_iterator = std::vector<uenv_record>::const_iterator;

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
    struct pathset {
        std::filesystem::path store;
        std::filesystem::path meta;
        std::filesystem::path squashfs;
    };

    using enum repo_mode;
    repository() = delete;

    repository(repository&&);
    repository(std::unique_ptr<repository_impl>);

    // copying a repository is not permitted
    repository(const repository&) = delete;

    // returns nullopt if the data base is in memory
    std::optional<std::filesystem::path> path() const;

    util::expected<record_set, std::string>
    query(const uenv_label& label) const;

    util::expected<void, std::string> add(const uenv_record&);
    util::expected<void, std::string> remove(const uenv_record&);
    util::expected<void, std::string> remove(const sha256&);

    // return true if the repository is readonly
    bool is_readonly() const;

    // return true if the repository is in memory
    bool is_in_memory() const;

    // return the paths that identify where the uenv image with sha256 and its
    // meta data would be stored in the repository if they were in the
    // repository. An image with sha256 does not need to be in the repository.
    pathset uenv_paths(sha256) const;

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
