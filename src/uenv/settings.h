#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <util/expected.h>

namespace uenv {

struct config_base {
    std::optional<std::string> repo;
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

// read configuration from a file
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path);

// get the default configuration
config_base default_config();

config_base merge(const config_base& lhs, const config_base& rhs);

struct configuration {
    std::optional<std::filesystem::path> repo;
    bool color;
    configuration& operator=(const configuration&) = default;
};

util::expected<configuration, std::string>
generate_configuration(const config_base& base);

} // namespace uenv
