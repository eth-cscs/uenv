#pragma once

#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <uenv/repository.h>
#include <util/envvars.h>

namespace uenv {

struct registry_config {
    std::string url;
    std::string default_namespace;
    std::optional<std::string> artifactory_url;
};

struct config_base {
    repo_list repos;
    std::optional<bool> color;
    std::optional<std::string> elastic_config;
    std::optional<std::string> system_name;
    std::optional<registry_config> registry;
};

// the result of parsing a line in a configuration file
// TODO: This was used in the original configuration file format.
//       Remove once all clusters have been upgraded to use a version of uenv
//       with TOML configuration files.
struct config_line {
    std::string key;
    std::string value;
    operator bool() const {
        return !key.empty();
    }
};

// holds an error message and line number from parsing a TOML configuration.
struct config_error {
    std::string message;
    std::uint32_t line;
};

// find the final configuration
// loads system and user configurations and merges them with the cli
// arguments and default settngs.
//
// the repos list is the list of repository labels provided using the --repo
// flag
util::expected<config_base, std::string>
load_config(const uenv::config_base& cli_config,
            const std::optional<std::vector<repo_label>>& repos,
            const envvars::state& calling_env);

struct configuration {
    uenv::repo_list repos;
    bool color;
    std::optional<std::string> elastic_config;
    std::optional<std::string> system_name;
    std::optional<registry_config> registry;
    std::optional<repo_description> default_repo;
    configuration& operator=(const configuration&) = default;

    // return:
    // - the default_repo if is is set (may or may not exist)
    // - else the first repo in repos (if repos is not empty)
    // - else nothing
    std::optional<uenv::repo_description> user_repo() const;
};

// performs additional validation on parsed user and config file inputs
configuration generate_configuration(const config_base& base);

std::optional<std::filesystem::path> system_config_path();
std::optional<std::filesystem::path>
user_config_path(const envvars::state& calling_env);

// helper that will return the default writeable repository, if it exists or can
// be created.
// used by modes that update a repo (image add, image rm, image pull, repo
// create) that need to pick one repo to modify.
// it will create the default repo, if one has been specified but has not yet
// been created.
util::expected<repository, std::string>
concretise_user_repo(const configuration&);

} // namespace uenv

template <> class fmt::formatter<uenv::config_error> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    template <typename FmtContext>
    constexpr auto format(uenv::config_error const& err,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "(line {}) {}", err.line, err.message);
    }
};

template <> class fmt::formatter<uenv::registry_config> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    template <typename FmtContext>
    constexpr auto format(uenv::registry_config const& config,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(),
                              "registry(url={}, default_namespace={})",
                              config.url, config.default_namespace);
    }
};
