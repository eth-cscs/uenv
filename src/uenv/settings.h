#pragma once

#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <uenv/repository.h>
#include <util/envvars.h>

namespace uenv {

struct config_base {
    repo_list repos;
    std::optional<bool> color;
    std::optional<std::string> elastic_config;
    std::optional<std::string> system_name;
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
    configuration& operator=(const configuration&) = default;

    std::optional<uenv::repo_description> repo() const;
};

// performs additional validation on parsed user and config file inputs
configuration generate_configuration(const config_base& base);

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
