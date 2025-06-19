#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <fmt/core.h>

#include <uenv/registry.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

struct config_base {
    std::optional<std::string> repo;
    std::optional<std::string> registry;
    std::optional<std::string> registry_type;
    std::optional<bool> color;
};

// the result of parsing a line in a configuration file
struct config_line {
    std::string key;
    std::string value;
    // evaluates to false -> an empty or comment line
    operator bool() const {
        return !key.empty();
    }
};

// read configuration from the user configuration file
// the location of the config file is determined using XDG_CONFIG_HOME or HOME
// if config_path is provided, use it directly instead of the default locations
util::expected<config_base, std::string> load_user_config(
    const envvars::state&,
    const std::optional<std::filesystem::path>& config_path = std::nullopt);

// get the default configuration
config_base default_config(const envvars::state& calling_env);

config_base merge(const config_base& lhs, const config_base& rhs);

struct configuration {
    std::optional<std::filesystem::path> repo;
    std::optional<std::filesystem::path> registry;
    registry_type registry_type_val;
    bool color;
    configuration& operator=(const configuration&) = default;
};

util::expected<configuration, std::string>
generate_configuration(const config_base& base);

} // namespace uenv

template <> class fmt::formatter<uenv::config_base> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::config_base const& cfg, FmtContext& ctx) const {
        return fmt::format_to(
            ctx.out(), "[repo: {}, registry: {}, registry_type: {}, color: {}]",
            cfg.repo.value_or("()"), cfg.registry.value_or("()"),
            cfg.registry_type.value_or("()"), cfg.color.value_or("()"));
    }
};
