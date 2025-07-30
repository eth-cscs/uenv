#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace envvars {

// used to indicate the method used to embed environment variable names in
// strings for expansion
//
// curly:  variables are delimited with '${' and '}', e.g:
//      "${CUDA_HOME}"
//      "${SCRATCH}/local-files"
//      "${HOME}/.config/${CLUSTER_NAME}"
//
// view:   used in env.json view definitions, to avoid conflicts.
//         variables are delimited with '${@' and '@}', e.g:
//      "${@CUDA_HOME@}"
//      "${@SCRATCH@}/local-files"
//      "${@HOME@}/.config/${@CLUSTER_NAME@}"
enum class expand_delim { curly, view };

// forward declare patch;

struct patch;

// The environment variable state of an environment.
// Environment variables are stored in a hash table with the variable name as
// the key.
// An interface for performing the standard get, set and unset operations for a
// variable is provided.
//
// Initialised using either:
//      - char**: used to capture the environment (typically througn the
//      ::environ global variable or envp argument to main)
//      - const state&: by copying.
//
// Using this representation, all calls to getenv and setenv system calls can be
// avoided:
//      - make a read only copy of the environment by grabbing ::environ or
//      envp when the application starts
//      - when launching a sub-process, make a mutable copy of the calling state
//      (or create an empty state)
//      - apply changes to the mutable copy using set, unset and clear methods
//      - use `c_env` to make a char** version of the state, and pass this to
//      `execve`
class state {
  public:
    state() = default;
    state(char* env[]);
    state(const state&) = default;

    void set(std::string_view name, std::string_view value);
    void unset(std::string_view name);
    std::optional<std::string> get(std::string_view name) const;

    const std::unordered_map<std::string, std::string> variables() const;
    void clear();

    char** c_env() const;
    std::string expand(std::string_view, expand_delim) const;

    // apply a patch to the environment
    void apply_patch(const patch&, expand_delim);

  private:
    std::unordered_map<std::string, std::string> variables_;
};

// helper utility function for freeing the memory of a c-style environ variable
// created using state::c_env
void c_env_free(char** env);

// represents a scalar environment variable.
// all environment variables are scalar, with the exception of prefix_path
// variables.
struct scalar {
    std::string name;

    // store the value in an optional.
    // nullopt implies that the variable is to be explicitly unset.
    std::optional<std::string> value;
};

bool operator==(const scalar& lhs, const scalar& rhs);

enum class update_kind { set, prepend, append, unset };

struct prefix_path_update {
    void apply(std::vector<std::string>&, bool& set);

    update_kind op;
    std::vector<std::string> values;
};

// represents a prefix_path environment variable
// prefix path variables are colon-separated lists of paths, e.g. PATH,
// LD_LIBRARY_PATH, PKG_CONFIG_PATH, PYTHONPATH, etc.
//
// These variables are represented as an ordered list of paths with associated
// set, unset, append and prepend operations, which capture "how" changes are to
// be applied to an initial list, equivalet to:
//  set:     export PATH=X
//  append:  export PATH=$PATH:X
//  prepend: export PATH=X:$PATH
//  unset:   unset  PATH
struct prefix_path {
    prefix_path(std::string name) : name_(std::move(name)) {};
    prefix_path() = delete;

    void update(prefix_path_update u);
    std::optional<std::string> get(const std::string& initial_value = "") const;

    std::string_view name() const {
        return name_;
    }
    const std::vector<prefix_path_update>& updates() const {
        return updates_;
    }

    // return true if the result of applying this is that the variable is unset
    bool unset() const;

  private:
    std::string name_;
    std::vector<prefix_path_update> updates_;
};

// a patch represents a set of changes to perform to envvars::state
// in practice, each uenv view is represented by a patch, that describes:
//      - a list of scalar variables to be set or unset
//      - a list of prefix_paths that describe how to overwrite or update the
//      existing value
struct patch {
    bool update_scalar(const std::string& name,
                       std::optional<std::string> value);
    bool update_prefix_path(const std::string& name, prefix_path_update update);
    std::vector<scalar> get_values(
        std::function<std::optional<std::string>(const std::string&)> f) const;

    const std::unordered_map<std::string, scalar>& scalars() const {
        return scalars_;
    };
    const std::unordered_map<std::string, prefix_path>& prefix_paths() const {
        return prefix_paths_;
    };

    void merge(const patch& other);

  private:
    std::unordered_map<std::string, scalar> scalars_;
    std::unordered_map<std::string, prefix_path> prefix_paths_;
};

// removes duplicate entries from a prefix path.
// prefix paths are searched in order, e.g. each path in PATH is searched in
// turn when looking for an executable, and the first match is used.
// Hence it is possible to remove duplicates, so long as the order is unchanged.
//
// environments can get very messy with many duplicates: use this to simplify
// prefix paths
std::vector<std::string>
simplify_prefix_path_list(const std::vector<std::string>& in);

} // namespace envvars

#include <fmt/core.h>

template <> class fmt::formatter<envvars::scalar> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(envvars::scalar const& s, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}={}", s.name,
                              s.value.value_or("<unset>"));
    }
};

template <> class fmt::formatter<envvars::update_kind> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(envvars::update_kind const& k,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}",
                              (k == envvars::update_kind::prepend  ? "prepend"
                               : k == envvars::update_kind::append ? "append"
                               : k == envvars::update_kind::set    ? "set"
                                                                   : "unset"));
    }
};

template <> class fmt::formatter<envvars::prefix_path_update> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(envvars::prefix_path_update const& u,
                          FmtContext& ctx) const {
        auto ctx_ = fmt::format_to(ctx.out(), "{}: [", u.op);
        for (auto& p : u.values) {
            fmt::format_to(ctx_, "{},", p);
        }
        return fmt::format_to(ctx, "]");
    }
};

template <> class fmt::formatter<envvars::prefix_path> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    auto format(envvars::prefix_path const& p, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}={}", p.name(),
                              p.get("$" + std::string(p.name())));
    }
};

template <> class fmt::formatter<envvars::patch> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    auto format(envvars::patch const& e, FmtContext& ctx) const {
        auto ctx_ = fmt::format_to(ctx.out(), "scalars:\n");
        for (auto s : e.scalars()) {
            ctx_ = fmt::format_to(ctx_, "  {}\n", s.second);
        }
        ctx_ = fmt::format_to(ctx_, "lists:\n");
        for (auto p : e.prefix_paths()) {
            ctx_ = fmt::format_to(ctx_, "  {}\n", p.second);
        }
        return ctx_;
    }
};
