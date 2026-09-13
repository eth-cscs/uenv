#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/core.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <uenv/meta.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

namespace {

using json = nlohmann::json;

// look up key in j: a pointer to the value, or nullptr when j is not an object
// or has no such key. nlohmann's const operator[] asserts (and is undefined in
// release builds) when the key is absent, so the meta data, which is not
// schema-checked, is only ever read through this.
const json* find(const json& j, const char* key) {
    if (!j.is_object()) {
        return nullptr;
    }
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

// the string value of j[key], or nullopt when absent or not a string.
std::optional<std::string> find_string(const json& j, const char* key) {
    if (auto v = find(j, key); v && v->is_string()) {
        return v->get<std::string>();
    }
    return std::nullopt;
}

// read the environment variable definitions of one view.
envvars::patch load_view_env(const json& env, const std::string& name,
                             const std::string& view_name) {
    envvars::patch envvars;

    const json* values = find(env, "values");
    if (const json* list = values ? find(*values, "list") : nullptr;
        list && list->is_object()) {
        for (const auto& [var_name, updates] : list->items()) {
            for (const auto& u : updates) {
                // if "op" is not one of "set", "append", "prepend" or "unset",
                // the default "unset" will be selected.
                const auto op = find_string(u, "op");
                const json* value = find(u, "value");
                if (!op || !value) {
                    // create an error if an invalid value was provided, but
                    // don't exit
                    spdlog::error("invalid prefix_list env variable definition "
                                  "for '{}' in view {}:{}",
                                  var_name, name, view_name);
                    continue;
                }
                const bool all_strings =
                    value->is_array() &&
                    std::all_of(value->begin(), value->end(),
                                [](const json& p) { return p.is_string(); });
                if (!all_strings) {
                    spdlog::error("invalid prefix_list value for '{}' in view "
                                  "{}:{}: expect an array of strings",
                                  var_name, name, view_name);
                    continue;
                }
                const envvars::update_kind kind =
                    op.value() == "append"    ? envvars::update_kind::append
                    : op.value() == "prepend" ? envvars::update_kind::prepend
                    : op.value() == "set"     ? envvars::update_kind::set
                                              : envvars::update_kind::unset;
                envvars.update_prefix_path(
                    var_name, {kind, value->get<std::vector<std::string>>()});
            }
        }
    }
    if (const json* scalar = values ? find(*values, "scalar") : nullptr;
        scalar && scalar->is_object()) {
        for (const auto& [var_name, val] : scalar->items()) {
            if (val.is_null()) {
                envvars.update_scalar(var_name, std::nullopt);
            } else if (val.is_string()) {
                envvars.update_scalar(var_name, val.get<std::string>());
            } else {
                // create an error if an invalid value was provided, but don't
                // exit
                spdlog::error("invalid scalar environment variable value (must "
                              "be string or null) '{}={}' in view {}:{}",
                              var_name, val.dump(), name, view_name);
            }
        }
    }

    return envvars;
}

} // namespace

// construct meta data from an input file
util::expected<meta, std::string> load_meta(const std::filesystem::path& file) {
    spdlog::debug("uenv::load_meta attempting to open uenv meta data file {}",
                  file.string());

    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return util::unexpected(fmt::format(
            "the uenv meta data file {} does not exist", file.string()));
    }
    spdlog::debug("uenv::load_meta file opened");

    auto fid = std::ifstream(file);

    json raw;
    try {
        raw = json::parse(fid);
    } catch (std::exception& e) {
        return util::unexpected(
            fmt::format("error parsing meta data file for uenv {}: {}",
                        file.string(), e.what()));
    }
    spdlog::debug("uenv::load_meta raw json read");

    if (!raw.is_object()) {
        return util::unexpected(
            fmt::format("error parsing meta data file for uenv {}: the "
                        "document is not a JSON object",
                        file.string()));
    }

    const std::string name = find_string(raw, "name").value_or("unnamed");
    const std::optional<std::string> description =
        find_string(raw, "description");
    const auto mount = find_string(raw, "mount");
    if (!mount) {
        return util::unexpected(
            fmt::format("error parsing meta data file for uenv {}: the "
                        "required field 'mount' is missing or not a string",
                        file.string()));
    }

    spdlog::debug("uenv::load_meta name '{}' mount {} description '{}'", name,
                  mount.value(), description);

    std::unordered_map<std::string, concrete_view> views;
    if (const json* jviews = find(raw, "views");
        jviews && jviews->is_object()) {
        for (const auto& [view_name, desc] : jviews->items()) {
            envvars::patch envvars;
            if (const json* env = find(desc, "env")) {
                envvars = load_view_env(*env, name, view_name);
            } else {
                spdlog::warn("uenv::load_meta view '{}:{}' contains only an "
                             "activation script - the view will not load "
                             "correctly with this version of uenv",
                             name, view_name);
            }

            const std::string view_description =
                find_string(desc, "description").value_or("");
            views[view_name] =
                concrete_view{view_name, view_description, envvars};
        }
    }

    std::optional<std::string> default_view = find_string(raw, "default-view");
    if (default_view && !views.contains(default_view.value())) {
        // it is not a hard error if the meta data is inconsistent.
        // instead we print a message, and continue without a default view.
        // this is a compromise to ensure that uenv do not fail to load
        // because of meta data problems.
        spdlog::error("internal error parsing uenv meta data in {}: the "
                      "default view {} matches no views in the uenv",
                      file.string(), default_view.value());
        default_view = std::nullopt;
    }

    return meta{.name = name,
                .description = description,
                .mount = mount.value(),
                .views = views,
                .default_view = default_view};
}

} // namespace uenv
