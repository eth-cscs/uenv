#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <util/envvars.h>

namespace uenv {

struct config_base {
    std::optional<std::string> repo;
    std::optional<bool> color;
    std::optional<std::string> elastic_config;
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

// load config
config_base load_config(const uenv::config_base&,
                        const envvars::state& calling_env);

// get the default configuration
config_base default_config(const envvars::state& calling_env);

config_base merge(const config_base& lhs, const config_base& rhs);

struct configuration {
    std::optional<std::filesystem::path> repo;
    bool color;
    std::optional<std::string> elastic_config;
    configuration& operator=(const configuration&) = default;
};

// performs additional validation on parsed user and config file inputs
configuration generate_configuration(const config_base& base);

} // namespace uenv
