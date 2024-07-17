#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <util/expected.h>

#include "envvars.h"

namespace uenv {

struct view_meta {
    std::string name;
    std::optional<std::string> description;
    uenv::envvarset env;
};

struct meta {
    // construct meta data from an input file
    std::string name;
    std::string description;
    std::unordered_map<std::string, view_meta> views;
};

// load a meta object from a json meta data file
// typically $mount/meta/env.json
util::expected<meta, std::string> load_meta(const std::filesystem::path&);

} // namespace uenv
