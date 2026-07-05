#include "environ.h"
#include <fmt/ranges.h>
#include <optional>
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
#include <spdlog/spdlog.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <uenv/log.h>
#include <uenv/ns_join.h>
#include <uenv/rootless.h>

//
// Forward declare the implementation of the plugin callbacks.
//
namespace impl {
int slurm_spank_task_init_sqfs_mount(spank_t sp, int ac, char** av);
int slurm_spank_task_init_sqfs_ll(spank_t sp);
} // namespace impl

extern "C" {

int slurm_spank_task_init(spank_t sp, [[maybe_unused]] int ac,
                          [[maybe_unused]] char** av) {
    // return impl::slurm_spank_task_init_sqfs_mount(sp, ac, av);
    return impl::slurm_spank_task_init_sqfs_ll(sp);
}

} // extern "C"

namespace impl {

// fork + call `squashfs_mount` executable
int slurm_spank_task_init_sqfs_mount(spank_t sp, int ac [[maybe_unused]],
                                     char** av [[maybe_unused]]) {
    uenv::init_log(spdlog::level::off);

    auto mount_var = uenv::slurm::getenv_wrapper(sp, "UENV_MOUNT_LIST");
    if (!mount_var) {
        return ESPANK_SUCCESS;
    }

    auto mounts = uenv::parse_and_validate_mounts(mount_var.value());
    if (!mounts) {
        slurm_error("%s", mounts.error().c_str());
        return -ESPANK_ERROR;
    }

    uint32_t job_id = 0;
    uint32_t step_id = 0;
    spank_get_item(sp, S_JOB_ID, &job_id);
    spank_get_item(sp, S_JOB_STEPID, &step_id);
    const auto join_tag = fmt::format("{}-{}", job_id, step_id);

    int ntasks = 1;
    spank_get_item(sp, S_JOB_LOCAL_TASK_COUNT, &ntasks);

    uenv::join_t join;
    if (auto r = uenv::join_begin(join, join_tag); !r) {
        slurm_error("join_begin failed: %s", r.error().c_str());
        return -ESPANK_ERROR;
    }

    std::optional<pid_t> winner_pid = std::nullopt;
    if (join.winner_p) {
        const auto mount_str =
            fmt::format("{}", fmt::join(mounts.value(), ","));

        winner_pid = fork();
        if (winner_pid.value() == 0) {
            prctl(PR_SET_PDEATHSIG, SIGHUP);
            execlp("squashfs-mount", "squashfs-mount", "--sqfs",
                   mount_str.c_str(), "--", "sh", "-c", "kill -STOP $$; exit 0",
                   nullptr);
            slurm_error("uenv: exec squashfs-mount failed: %s",
                        strerror(errno));
            return -ESPANK_ERROR;
        } else if (winner_pid.value() < 0) {
            slurm_error("uenv: fork failed: %s", strerror(errno));
            return -ESPANK_ERROR;
        }

        // wait until squashfs-mount-rootless stops itself after mounting
        siginfo_t sig_info;
        if (waitid(P_PID, winner_pid.value(), &sig_info, WSTOPPED) < 0) {
            slurm_error("uenv: waitid failed: %s", strerror(errno));
            return -ESPANK_ERROR;
        }

        // enter the user+mount namespace created by squashfs-mount-rootless;
        // join_end will record winner_pid = getpid() so losing tasks can join
        // the same namespace
        if (auto r = uenv::namespaces_join(winner_pid.value()); !r) {
            slurm_error("namespaces_join failed: %s", r.error().c_str());
            return -ESPANK_ERROR;
        }
    } else {
        // winner_pid is the winner task's PID after it entered the squashfs
        // namespace, so joining its namespaces gives us the same view
        auto r = uenv::namespaces_join(join.shared->winner_pid);
        if (!r) {
            slurm_error("namespaces_join failed: %s", r.error().c_str());
            return -ESPANK_ERROR;
        }
    }

    if (auto r = uenv::join_end(join, ntasks, winner_pid /* the winner pid */);
        !r) {
        slurm_error("join_end failed: %s", r.error().c_str());
        return -ESPANK_ERROR;
    }

    return ESPANK_SUCCESS;
}

// use the squashfuse_ll interface
int slurm_spank_task_init_sqfs_ll(spank_t sp) {
    uenv::init_log(spdlog::level::off);

    // parse environment variables to test whether there is anything to
    // mount
    auto mount_var = uenv::slurm::getenv_wrapper(sp, "UENV_MOUNT_LIST");

    // variable is not set - nothing to do here
    if (!mount_var) {
        return ESPANK_SUCCESS;
    }

    const uid_t uid = getuid();
    const uid_t gid = getgid();

    int ntasks = 1;
    uint32_t job_id = 0;
    uint32_t step_id = 0;
    spank_get_item(sp, S_JOB_LOCAL_TASK_COUNT, &ntasks);
    spank_get_item(sp, S_JOB_ID, &job_id);
    spank_get_item(sp, S_JOB_STEPID, &step_id);
    const auto join_tag = fmt::format("{}-{}", job_id, step_id);

    // parse and validate the mount descriptions
    // note that it is very important to carefully validate the mount_list
    // * check that the squashfs files exist and can be read by the user
    // * check that the mount points exist
    auto mounts = uenv::parse_and_validate_mounts(mount_var.value());
    if (!mounts) {
        slurm_error("%s", mounts.error().c_str());
        return -ESPANK_ERROR;
    }

    uenv::join_t join;
    if (auto r = uenv::join_begin(join, join_tag); !r) {
        slurm_error("%s", r.error().c_str());
        return -ESPANK_ERROR;
    }

    if (join.winner_p) {
        if (auto result = uenv::rootless::unshare_mount_map_root(); !result) {
            slurm_error("%s", result.error().c_str());
            return -ESPANK_ERROR;
        }

        for (auto mount_pair : mounts.value()) {
            if (auto result = uenv::rootless::do_sqfs_ll_mount(
                    mount_pair, true /*use multi threaded fuse*/);
                !result) {
                slurm_error("error mounting the requested uenv image: %s",
                            result.error().c_str());
                return -ESPANK_ERROR;
            }
        }

        // exit fake-root
        if (auto r = uenv::rootless::map_effective_user(uid, gid); !r) {
            slurm_error("failed map effective user %s %d %d", r.error().c_str(),
                        uid, gid);
            return -ESPANK_ERROR;
        }

        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            slurm_error("PR_SET_NO_NEW_PRIVS failed");
            return -ESPANK_ERROR;
        }
    } else {
        // winner_pid is the winner task's PID after it entered the squashfs
        // namespace, so joining its namespaces gives us the same view
        auto r = uenv::namespaces_join(join.shared->winner_pid);
        if (!r) {
            slurm_error("namespaces_join failed: %s", r.error().c_str());
            return -ESPANK_ERROR;
        }
    }

    if (auto r = uenv::join_end(join, ntasks, std::nullopt); !r) {
        slurm_error("join_end failed %s", r.error().c_str());
        return -ESPANK_ERROR;
    }

    return ESPANK_SUCCESS;
}
} // namespace impl
