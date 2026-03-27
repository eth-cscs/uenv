#pragma once

#include <concepts>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/std.h>

#include <fmt/format.h>

#include <uenv/uenv.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

// A name=path pair from the CLI (e.g. --repo myrepo=/path/to/repo).
// Distinct from repo_description, which carries priority for config-layer
// ordering and is the fully resolved form used everywhere else.
struct repo_name_path {
    std::string name;
    std::filesystem::path path;
};

struct repo_description {
    static constexpr std::uint32_t default_priority = 10;

    std::string name;
    std::filesystem::path path;
    std::uint32_t priority = default_priority;
    friend bool operator<(const repo_description& lhs,
                          const repo_description& rhs);
    friend bool operator==(const repo_description& lhs,
                           const repo_description& rhs);
};

template <typename T>
concept repo_label_type =
    std::same_as<T, std::string> || std::same_as<T, std::filesystem::path> ||
    std::same_as<T, repo_name_path>;

class repo_label {
  public:
    template <repo_label_type T>
    repo_label(T value) : value_{std::move(value)} {};

    bool is_name() const;
    bool is_path() const;
    bool is_name_path() const;
    const std::string& as_name() const;
    const std::filesystem::path& as_path() const;
    const repo_name_path& as_name_path() const;

  private:
    std::variant<std::string, std::filesystem::path, repo_name_path> value_;
};

util::expected<repo_description, std::string>
pick_repo(const repo_label& label,
          const std::vector<repo_description>& descriptions,
          const std::string& altname);

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
validate_repo_path(const std::filesystem::path& path, bool is_absolute = true,
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
                              d.name, d.path.string(), d.priority);
    }
};

template <> class fmt::formatter<uenv::repo_label> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::repo_label const& d, FmtContext& ctx) const {
        return d.is_name() ? fmt::format_to(ctx.out(), "[name={}]", d.as_name())
               : d.is_path() ? fmt::format_to(ctx.out(), "[path={}]",
                                              d.as_path().string())
                             : fmt::format_to(ctx.out(), "[name={}, path={}]",
                                              d.as_name_path().name,
                                              d.as_name_path().path.string());
    }
};
