#pragma once

#include <optional>
#include <string>
#include <vector>

#include <uenv/env.h>
#include <util/expected.h>

namespace uenv {

struct telemetry_data {
    std::string mount;
    std::string sqfs;
    std::optional<std::string> digest;
    std::vector<std::string> views;
    std::optional<std::string> label;
    std::string name;
};

std::string to_string(const std::vector<telemetry_data>&);

util::expected<std::vector<telemetry_data>, std::string>
parse_telemetry(const std::string&);

std::vector<telemetry_data> make_telemetry(const env&);

util::expected<std::vector<telemetry_data>, std::string>
telemetry_from_env(const envvars::state& calling_environment);

} // namespace uenv
