#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <util/expected.h>

#include "view.h"

namespace uenv {

struct meta {
    // construct meta data from an input file
    std::string name;
    std::string description;
    std::unordered_map<std::string, concrete_view> views;
};

// load a meta object from a json meta data file
// typically $mount/meta/env.json
util::expected<meta, std::string> load_meta(const std::filesystem::path&);

} // namespace uenv
