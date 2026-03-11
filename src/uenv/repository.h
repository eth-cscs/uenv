#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <uenv/uenv.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

struct repo_description {
    static constexpr std::uint32_t default_priority = 10;

    std::string name;
    std::string path;
    std::uint32_t priority = default_priority;
    friend bool operator<(const repo_description& lhs,
                          const repo_description& rhs);
};

class repo_label {
  public:
    template <typename T> repo_label(T value) : value_{std::move(value)} {};

    bool is_name() const;
    bool is_path() const;
    bool is_description() const;
    const std::string& as_name() const;
    const std::filesystem::path& as_path() const;
    const repo_description& as_description() const;

  private:
    std::variant<std::string, std::filesystem::path, repo_description> value_;
};

util::expected<std::vector<repo_description>, std::string>
filter_repo_list(const std::vector<repo_label>& labels,
                 const std::vector<repo_description>& descriptions,
                 bool validate = true);

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
/// - use $SCRATCH/.uenv-images if $SCRATCH is set
/// - use $HOME/.uenv/images
// std::optional<std::string> default_repo_path(const envvars::state&);
std::optional<std::filesystem::path>
default_repo_path(const envvars::state& env, bool exists = false);

// used to validate whether a string represents a valid path for a repo.
// parses the string, then performs additional optional checks for whether
// the path is absolute / already exists. e.g.
// validate_repository("/home/bob/.uenv", true, false);
// - the path is valid and absolute
// - so it will return true always (because no check for existance is
// performed)
util::expected<std::filesystem::path, std::string>
validate_repo_path(const std::string& path, bool is_absolute = true,
                   bool exists = true);

enum class repo_state { readonly, readwrite, no_exist, invalid };
// assumes that repo_path satisfies valide_repo_path()
repo_state validate_repository(const std::filesystem::path& repo_path);

enum class repo_mode : std::uint8_t { readonly, readwrite };

struct repository_impl;
struct repository {
  private:
    std::unique_ptr<repository_impl> impl_;

  public:
    struct pathset {
        std::filesystem::path root;
        std::filesystem::path store_root;
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
    // search for all records that match a label.
    // the partial_name parameter toggles on partial matches on the
    // label.name field, which is useful for `image ls`  `image find` searches.
    query(const uenv_label& label, bool partial_name = false) const;

    bool contains(const uenv_record&) const;

    util::expected<void, std::string> add(const uenv_record&);
    util::expected<record_set, std::string> remove(const uenv_record&);
    util::expected<record_set, std::string> remove(const sha256&);

    // return true if the repository is readonly
    bool is_readonly() const;

    // return true if the repository is in memory
    bool is_in_memory() const;

    // return the paths that identify where the uenv image with sha256 and
    // its meta data would be stored in the repository if they were in the
    // repository. An image with sha256 does not need to be in the
    // repository.
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

template <> class fmt::formatter<uenv::repo_state> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::repo_state const& e, FmtContext& ctx) const {
        using enum uenv::repo_state;
        switch (e) {
        case readonly:
            return format_to(ctx.out(), "readonly");
        case readwrite:
            return format_to(ctx.out(), "readwrite");
        case no_exist:
            return format_to(ctx.out(), "no_exist");
        case invalid:
            return format_to(ctx.out(), "invalid");
        }
        return format_to(ctx.out(), "unknonwn");
    }
};

template <> class fmt::formatter<uenv::repo_description> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::repo_description const& d,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "[name={}, path={}, priority={}]",
                              d.name, d.path, d.priority);
    }
};
