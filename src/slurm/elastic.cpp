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
        if (!config.elastic_config) {
            exit(0);
        }
        nlohmann::json data;

        if (auto digest = calling_env.get("UENV_DIGEST_LIST"); digest) {
            data["digest"] = *digest;
        }
        if (auto mount_list = calling_env.get("UENV_MOUNT_LIST"); mount_list) {
            data["mount_list"] = *mount_list;
        }
        const auto now = std::chrono::system_clock::now();
        const std::time_t t_c = std::chrono::system_clock::to_time_t(now);
        auto time = std::ctime(&t_c);
        data["time"] = time;

        if (auto slurm_stepid = calling_env.get("SLURM_STEPID"); slurm_stepid) {
            data["stepid"] = *slurm_stepid;
        } else {
            spdlog::warn("slurm stepid not defined");
        }
        if (auto slurm_jobid = calling_env.get("SLURM_JOBID"); slurm_jobid) {
            data["jobid"] = *slurm_jobid;
        } else {
            spdlog::warn("slurm jobid not defined");
        }

        if (config.elastic_config) {
            auto reply = util::curl::post(data, *config.elastic_config);
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
