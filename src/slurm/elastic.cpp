#include "elastic.h"
#include "spdlog/common.h"
#include "uenv/config.h"
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
#include <uenv/parse.h>
#include <unistd.h>
#include <util/curl.h>
#include <util/expected.h>

namespace uenv {

void elasticsearch_statistics(const envvars::state& calling_env) {
    int pid = fork();
    if (pid < 0) {
        // fork failed
    } else if (pid == 0) {
        // Child process
        int null_fd = open("/dev/null", O_RDWR); // Open once
        if (null_fd != -1) {
            dup2(null_fd, STDIN_FILENO);  // Redirect stdin to /dev/null
            dup2(null_fd, STDOUT_FILENO); // Redirect stdout to /dev/null
            dup2(null_fd, STDERR_FILENO); // Redirect stderr to /dev/null

            if (null_fd > 2)
                close(null_fd); // Close extra fd if not 0,1,2
        }

        auto config = load_config(uenv::config_base{}, calling_env);
        if (!config.elastic_config) {
            exit(0);
        }
        nlohmann::json data;

        if (auto mount_list = calling_env.get("UENV_MOUNT_DIGEST_LIST");
            mount_list) {
            data["mount_list_digest"] = nlohmann::json::array();
            if (auto r = parse_elastic_entry(*mount_list); r) {
                nlohmann::json entry;
                for (auto e : *r) {
                    entry["mountpoint"] = e.mount_point;
                    entry["sqfs"] = e.sqfs;
                    if (e.digest) {
                        entry["digest"] = *e.digest;
                    }
                    data["mount_list_digest"].push_back(entry);
                }
            }
        }

        const auto now = std::chrono::system_clock::now();
        const std::time_t t_c = std::chrono::system_clock::to_time_t(now);
        auto time = std::ctime(&t_c);
        data["time"] = time;
        data["uenv_version"] = UENV_VERSION;

        if (auto slurm_stepid = calling_env.get("SLURM_STEPID"); slurm_stepid) {
            data["stepid"] = *slurm_stepid;
        } else {
            data["stepid"] = nullptr;
        }
        if (auto slurm_jobid = calling_env.get("SLURM_JOBID"); slurm_jobid) {
            data["jobid"] = *slurm_jobid;
        } else {
            data["jobid"] = nullptr;
        }

        if (config.elastic_config) {
            util::curl::post(data, *config.elastic_config);
        }
        exit(0);
    }
}

} // namespace uenv
