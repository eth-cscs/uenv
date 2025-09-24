#pragma once

#include <string>

#include <util/envvars.h>
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

util::expected<std::vector<std::string>, std::string>
slurm_elastic_payload(const std::vector<telemetry_data>& data,
                      const envvars::state& calling_env);

} // namespace uenv
