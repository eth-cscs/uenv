#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#include "cscs.h"

namespace cscs {

std::optional<std::string> get_system_name(std::optional<std::string> value) {
    if (value) {
        if (value == "*") {
            return std::nullopt;
        }
        return value;
    }

    if (auto system_name = std::getenv("CLUSTER_NAME")) {
        spdlog::debug("cluster name is '{}'", system_name);
        return system_name;
    }

    spdlog::debug("cluster name is undefined");
    return std::nullopt;
}

} // namespace cscs
