#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <fmt/core.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <util/expected.h>

#include "envvars.h"
#include "meta.h"

namespace uenv {

// construct meta data from an input file
util::expected<meta, std::string> load_meta(const std::filesystem::path& file) {
    using json = nlohmann::json;

    spdlog::debug("uenv::load_meta attempting to open uenv meta data file {}",
                  file.string());

    try {
        if (!std::filesystem::is_regular_file(file)) {
            return util::unexpected(fmt::format(
                "the uenv meta data file {} does not exist", file.string()));
        }
        spdlog::debug("uenv::load_meta file opened");

        auto fid = std::ifstream(file);

        nlohmann::json raw;
        try {
            raw = json::parse(fid);
        } catch (std::exception& e) {
            return util::unexpected(
                fmt::format("error parsing meta data file for uenv {}: {}",
                            file.string(), e.what()));
        }
        spdlog::debug("uenv::load_meta raw json read");

        const std::string name = raw.contains("name") ? raw["name"] : "unnamed";
        using ostring = std::optional<std::string>;
        const ostring description =
            (raw.contains("description") && !raw["description"].is_null())
                ? ostring(raw["description"])
                : ostring{};
        const ostring mount =
            raw.contains("mount") ? ostring(raw["mount"]) : ostring{};

        spdlog::debug("uenv::load_meta name '{}' mount {} description '{}'",
                      name, mount, description);

        std::unordered_map<std::string, concrete_view> views;
        if (auto& jviews = raw["views"]; jviews.is_object()) {
            for (auto& [name, desc] : jviews.items()) {
                uenv::envvarset envvars;
                if (auto& list = desc["env"]["values"]["list"];
                    list.is_object()) {
                    for (auto& [var_name, updates] : list.items()) {
                        for (auto& u : updates) {
                            // todo: check whether "op" field exists
                            // todo: handle case where "op" is not one of "set",
                            // "append", "prepend" or "unset"
                            const uenv::update_kind op =
                                u["op"] == "append" ? uenv::update_kind::append
                                : u["op"] == "prepend"
                                    ? uenv::update_kind::prepend
                                : u["op"] == "set" ? uenv::update_kind::set
                                                   : uenv::update_kind::unset;
                            std::vector<std::string> paths = u["value"];
                            envvars.update_prefix_path(var_name, {op, paths});
                        }
                    }
                }
                if (auto& scalar = desc["env"]["values"]["scalar"];
                    scalar.is_object()) {
                    // todo: check that val is a string
                    // todo handle NULL case -> unset
                    for (auto& [var_name, val] : scalar.items()) {
                        envvars.update_scalar(var_name, val);
                    }
                }

                const std::string description =
                    desc.contains("description") ? desc["description"] : "";
                views[name] = concrete_view{name, description, envvars};
            }
        }

        return meta{name, description, mount, views};
    } catch (json::exception& e) {
        return util::unexpected(
            fmt::format("internal error parsing uenv meta data in {}: {}",
                        file.string(), e.what()));
    }
}

} // namespace uenv
