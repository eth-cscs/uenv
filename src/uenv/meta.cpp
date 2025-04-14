#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <fmt/core.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <uenv/meta.h>
#include <util/envvars.h>
#include <util/expected.h>

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

        // error handling here is as ugly as home made sin - not much we can do
        // about that without resorting to JSON schema.
        std::unordered_map<std::string, concrete_view> views;
        if (auto& jviews = raw["views"]; jviews.is_object()) {
            for (auto& [view_name, desc] : jviews.items()) {
                envvars::patch envvars;
                const bool has_env = desc.contains("env");
                if (!has_env) {
                    spdlog::warn(
                        "uenv::load_meta view '{}:{}' contains only an "
                        "activation script - the view will not load "
                        "correctly with this version of uenv",
                        name, view_name);
                } else {
                    const auto& env = desc["env"];
                    if (auto& list = env["values"]["list"]; list.is_object()) {
                        for (auto& [var_name, updates] : list.items()) {
                            for (auto& u : updates) {
                                // if "op" is not one of "set", "append",
                                // "prepend" or "unset", the default "unset"
                                // will be selected.
                                if (u.contains("op") && u.contains("value")) {
                                    const envvars::update_kind op =
                                        u["op"] == "append"
                                            ? envvars::update_kind::append
                                        : u["op"] == "prepend"
                                            ? envvars::update_kind::prepend
                                        : u["op"] == "set"
                                            ? envvars::update_kind::set
                                            : envvars::update_kind::unset;
                                    if (u["value"].is_array()) {
                                        std::vector<std::string> paths =
                                            u["value"];
                                        envvars.update_prefix_path(var_name,
                                                                   {op, paths});
                                    } else {
                                        spdlog::error("invalid prefix_list "
                                                      "value: expect an "
                                                      "array of strings: '{}'",
                                                      var_name);
                                    }
                                } else {
                                    // create an error if an invalid value was
                                    // provided, but don't exit
                                    spdlog::error(
                                        "invalid prefix_list env variable "
                                        "definition for '{}'",
                                        var_name);
                                }
                            }
                        }
                    }
                    if (auto& scalar = env["values"]["scalar"];
                        scalar.is_object()) {
                        for (auto& [var_name, val] : scalar.items()) {
                            if (val.is_null()) {
                                envvars.update_scalar(var_name, std::nullopt);
                            } else if (val.is_string()) {
                                envvars.update_scalar(var_name, val);
                            } else {
                                // create an error if an invalid value was
                                // provided, but don't exit
                                spdlog::error(
                                    "invalid scalar environment variable value "
                                    "(must be string or null) '{}={}'",
                                    var_name, std::string(val));
                            }
                        }
                    }
                }

                const std::string description =
                    desc.contains("description") ? desc["description"] : "";
                views[view_name] =
                    concrete_view{view_name, description, envvars};
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
