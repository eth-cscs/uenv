#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <slurm/spank.h>
#include <spdlog/spdlog.h>

#include <slurm/elastic.h>
#include <uenv/config.h>
#include <uenv/log.h>
#include <uenv/parse.h>
#include <uenv/settings.h>
#include <util/curl.h>
#include <util/expected.h>

namespace uenv {

util::expected<std::vector<std::string>, std::string>
slurm_elastic_payload(const std::vector<telemetry_data>& uenv_data,
                      const envvars::state& calling_env) {
    // handle exceptions thrown by nlohmann::json
    try {

        nlohmann::json data;

        // timestamp in ISO 8601 format
        data["@timestamp"] =
            fmt::format("{:%FT%T%z}", fmt::localtime(std::time(nullptr)));
        data["data_stream"] = nlohmann::json{{"type", "logs"},
                                             {"dataset", "telemetry.uenv"},
                                             {"namespace", "slurm.v01"}};

        // The Slurm jobid and stepid are required
        if (auto slurm_stepid = calling_env.get("SLURM_STEPID")) {
            data["stepid"] = *slurm_stepid;
        } else {
            return util::unexpected{"SLURM_STEPID is not set"};
        }
        if (auto slurm_jobid = calling_env.get("SLURM_JOBID")) {
            data["jobid"] = *slurm_jobid;
        } else {
            return util::unexpected{"SLURM_JOBID is not set"};
        }

        // fields that can be "null" are set as empty strings.
        // this is because elastic does not like the types of fields to differ
        // over time, and setting an unset field to null would violate that.
        data["cluster"] = calling_env.get("CLUSTER_NAME").value_or("");
        data["user"] = calling_env.get("USER").value_or("");
        data["host"] = calling_env.get("HOSTNAME").value_or("");
        data["uenv_version"] = UENV_VERSION;

        std::vector<std::string> payloads;
        for (auto& u : uenv_data) {
            auto payload = data;

            payload["mount"] = u.mount;
            payload["sqfs"] = u.sqfs;
            payload["digest"] = u.digest.value_or("");

            payloads.push_back(payload.dump());
        }

        return payloads;
    } catch (std::exception const& e) {
        return util::unexpected{e.what()};
    }
}

} // namespace uenv
