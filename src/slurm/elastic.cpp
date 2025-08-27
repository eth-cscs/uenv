#include "elastic.h"
#include "spdlog/common.h"
#include "uenv/settings.h"
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <slurm/spank.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <uenv/log.h>
#include <unistd.h>
#include <util/curl.h>

namespace uenv {

void elasticsearch_statistics(const envvars::state& calling_env) {
    int pid = fork();
    if (pid < 0) {
        // fork failed
    } else if (pid == 0) {
        auto config = load_config(uenv::config_base{}, calling_env);
        if (!config.ec) {
            exit(0);
        }
        nlohmann::json data;
        auto slurm_stepid = calling_env.get("SLURM_STEPID");
        auto slurm_jobid = calling_env.get("SLURM_JOBID");
        auto sha256 = calling_env.get("UENV_SHA256_LIST");
        if (sha256) {
            data["sha256"] = *sha256;
        } else {
            // the uenv has been started from file, skip sending stats
            spdlog::warn("uenv sha256 not defined");
            exit(0);
        }
        const auto now = std::chrono::system_clock::now();
        const std::time_t t_c = std::chrono::system_clock::to_time_t(now);
        auto time = std::ctime(&t_c);
        data["time"] = time;

        if (slurm_stepid) {
            data["stepid"] = *slurm_stepid;
        } else {
            spdlog::warn("slurm stepid not defined");
        }
        if (slurm_jobid) {
            data["jobid"] = *slurm_jobid;
        } else {
            spdlog::warn("slurm jobid not defined");
        }

        if (config.ec) {
            auto reply = util::curl::post(data, *config.ec);
            if (reply) {
                spdlog::trace("elastic reply {}", *reply);
            } else {
                spdlog::warn("curl post failed {} {}",
                             static_cast<int>(reply.error().code),
                             reply.error().message);
            }
        }
        exit(0);
    }
}

} // namespace uenv
