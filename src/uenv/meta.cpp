#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/core.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>

#include <util/expected.h>

#include "meta.h"

namespace uenv {

// construct meta data from an input file
util::expected<meta, std::string> load_meta(const std::filesystem::path& file) {
    using json = nlohmann::json;

    if (!std::filesystem::is_regular_file(file)) {
        return util::unexpected(fmt::format(
            "unable to read meta data file {}: it is not a file.", file));
    }

    auto fid = std::ifstream(file);

    // TODO: check parse
    const auto raw = json::parse(fid);

    // meta
    // std::string name;
    // std::string description;
    // std::unordered_map<std::string, view_meta> views;

    std::unordered_map<std::string, view_meta> views;
    if (auto& jviews = raw["views"]; jviews.is_object()) {
        for (auto& [name, desc] : jviews.items()) {
            uenv::envvarset envvars;
            if (auto& list = desc["env"]["values"]["list"]; list.is_object()) {
                for (auto& [nme, updates] : list.items()) {
                    // v is a list of updates applied to the prefix_path nme
                    for (auto& u : updates) {
                        uenv::update_kind op =
                            u["op"] == "append"    ? uenv::update_kind::append
                            : u["op"] == "prepend" ? uenv::update_kind::prepend
                                                   : uenv::update_kind::set;
                        std::vector<std::string> paths = u["value"];
                        envvars.update_prefix_path(nme, {op, paths});
                    }
                }
            }
            if (auto& scalar = desc["env"]["values"]["scalar"];
                scalar.is_object()) {
                for (auto& [nme, val] : scalar.items()) {
                    envvars.update_scalar(nme, val);
                }
            }
            // TODO: add description
            views[name] = view_meta{name, "", envvars};
        }
    }

    // todo add the name and description
    return meta{"name", "description", views};
}

} // namespace uenv
