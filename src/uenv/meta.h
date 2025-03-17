#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <util/expected.h>

#include "view.h"

namespace uenv {

// Stores uenv meta data loaded from the menta/env.json file inside the uenv
// squashfs.
struct meta {
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mount;
    std::unordered_map<std::string, concrete_view> views;
};

// load a meta object from a json meta data file
// typically $mount/meta/env.json
util::expected<meta, std::string> load_meta(const std::filesystem::path&);

} // namespace uenv
