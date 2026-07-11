#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/telemetry.h>
#include <uenv/uenv.h>
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
            telemetry_data T;

            const std::string mount = entry["mount"];
            const auto mount_r = uenv::parse_path(mount);
            if (!mount_r) {
                return util::unexpected{fmt::format("invalid mount '{}': {}",
                                                    mount,
                                                    mount_r.error().message())};
            }
            T.mount = mount_r.value();

            const std::string sqfs = entry["sqfs"];
            const auto sqfs_r = uenv::parse_path(sqfs);
            if (!sqfs_r) {
                return util::unexpected{fmt::format(
                    "invalid sqfs '{}': {}", sqfs, sqfs_r.error().message())};
            }
            T.sqfs = sqfs_r.value();

            T.digest = to_optional(entry["digest"]);
            if (T.digest && !util::sha256::parse(T.digest.value())) {
                return util::unexpected{fmt::format(
                    "invalid digest '{}': expected 64-character hex string",
                    T.digest.value())};
            }

            T.label = to_optional(entry["label"]);
            if (T.label) {
                const auto label_r = uenv::parse_uenv_label(T.label.value());
                if (!label_r) {
                    return util::unexpected{
                        fmt::format("invalid label '{}': {}", T.label.value(),
                                    label_r.error().message())};
                }
                T.label = fmt::format("{}", label_r.value());
            }

            T.name = entry["name"].get<std::string>();
            if (T.name.empty()) {
                return util::unexpected{"uenv name is empty"};
            }

            T.views = entry["views"].get<std::vector<std::string>>();
            for (const auto& v : T.views) {
                if (v.empty()) {
                    return util::unexpected{"view name is empty"};
                }
            }

            result.push_back(std::move(T));
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

util::expected<std::vector<telemetry_data>, std::string>
telemetry_from_env(const envvars::state& calling_environment) {
    if (const auto var = calling_environment.get("UENV_TELEMETRY")) {
        if (const auto result = parse_telemetry(var.value())) {
            return result.value();
        }
        spdlog::warn("UENV_TELEMETRY is set but could not be parsed; "
                     "falling back to UENV_MOUNT_LIST");
    }

    if (const auto mounts_var = calling_environment.get("UENV_MOUNT_LIST")) {
        const auto views = parse_env_view_description(
            calling_environment.get("UENV_VIEW").value_or(""));
        if (const auto mounts = uenv::parse_mount_list(*mounts_var)) {
            std::vector<telemetry_data> telemetry;
            for (auto& m : *mounts) {
                telemetry_data T{};
                T.mount = m.mount_path;
                T.sqfs = m.sqfs_path;
                T.digest = std::nullopt;
                T.label = std::nullopt;
                if (views) {
                    for (auto& v : *views) {
                        if (v.mount == m.mount_path) {
                            T.views.push_back(v.name);
                        }
                    }
                }
                // TODO: we could peek inside the squashfs
                T.name = "unknown";
                telemetry.push_back(std::move(T));
            }
            return telemetry;
        } else {
            return util::unexpected{
                fmt::format("UENV_MOUNT_LIST is malformed: {}",
                            mounts.error().message().c_str())};
        }
    }
    return util::unexpected{"unable to load uenv information from environment: "
                            "neither UENV_TELEMETRY nor UENV_MOUNT_LIST are "
                            "available and configured correctly"};
}

} // namespace uenv
