#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <util/expected.h>

namespace uenv {

struct config_base {
    std::optional<std::string> repo;
    std::optional<std::string> color;
};

// read configuration from a file
util::expected<config_base, std::string>
read_config(const std::filesystem::path& path);

// get the default configuration
config_base default_config();

config_base config_merge(const config_base& lhs, const config_base& rhs);

struct config {
    config(const config_base&);
    std::filesystem::path repo;
};

} // namespace uenv
