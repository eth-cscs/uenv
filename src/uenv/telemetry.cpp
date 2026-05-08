#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <uenv/telemetry.h>
#include <util/expected.h>

namespace uenv {

std::string to_string(const std::vector<telemetry_data>& telemetry) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& T : telemetry) {
        nlohmann::json data = {{"mount", T.mount},
                               {"sqfs", T.sqfs},
                               {"views", T.views},
                               {"name", T.name}};

        if (T.digest) {
            data["digest"] = *T.digest;
        } else {
            data["digest"] = nullptr;
        }
        if (T.label) {
            data["label"] = *T.label;
        } else {
            data["label"] = nullptr;
        }

        result.push_back(data);
    }

    return result.dump();
}

util::expected<std::vector<telemetry_data>, std::string>
parse_telemetry(const std::string& s) {
    // TODO: apply validation to all of the values in a telemetry field
    try {
        const auto data = nlohmann::json::parse(s);

        auto to_optional =
            [](const nlohmann::json& in) -> std::optional<std::string> {
            if (in.is_null()) {
                return std::nullopt;
            }
            return in.get<std::string>();
        };

        if (!data.is_array()) {
            return util::unexpected{
                "telemetry string is not an array of telemetry entries"};
        }
        std::vector<telemetry_data> result;
        for (auto& entry : data) {
            result.push_back({.mount = entry["mount"],
                              .sqfs = entry["sqfs"],
                              .digest = to_optional(entry["digest"]),
                              .views = entry["views"],
                              .label = to_optional(entry["label"]),
                              .name = entry["name"]});
        }

        return result;
    } catch (const std::exception& e) {
        return util::unexpected{e.what()};
    }
}

std::vector<telemetry_data> make_telemetry(const env& E) {
    std::vector<telemetry_data> telemetry;
    for (auto& [_, u] : E.uenvs) {
        // construct a list of views from this uenv that are used
        std::vector<std::string> views;
        for (const auto& v : E.views) {
            if (v.uenv == u.name) {
                views.push_back(v.name);
            }
        }

        // build the telemetry data
        telemetry.push_back({
            .mount = u.mount_path.string(),
            .sqfs = u.sqfs_path.string(),
            .digest = u.digest,
            .views = std::move(views),
            .label = u.label,
            .name = u.name,
        });
    }

    return telemetry;
}

} // namespace uenv
