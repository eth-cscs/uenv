#pragma once

#include <string>

#include <uenv/telemetry.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace uenv {

util::expected<std::vector<std::string>, std::string>
slurm_elastic_payload(const std::vector<telemetry_data>& data,
                      const envvars::state& calling_env);

} // namespace uenv
