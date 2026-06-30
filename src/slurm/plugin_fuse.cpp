#include "environ.h"
#include <fmt/ranges.h>
#include <slurm/spank.h>
#include <spdlog/spdlog.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <uenv/log.h>
#include <uenv/rootless.h>

namespace impl {

int slurm_spank_task_init(spank_t sp, int ac [[maybe_unused]],
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
    uenv::join_begin(join, join_tag);

    pid_t pid = -1;
    if (join.winner_p) {
        const auto mount_str =
            fmt::format("{}", fmt::join(mounts.value(), ","));

        pid = fork();
        if (pid == 0) {
            prctl(PR_SET_PDEATHSIG, SIGHUP);
            execlp("squashfs-mount", "squashfs-mount", "--sqfs",
                   mount_str.c_str(), "--", "sh", "-c", "kill -STOP $$; exit 0",
                   nullptr);
            slurm_error("uenv: exec squashfs-mount failed: %s",
                        strerror(errno));
            _exit(EXIT_FAILURE);
        } else if (pid < 0) {
            slurm_error("uenv: fork failed: %s", strerror(errno));
            return -ESPANK_ERROR;
        }

        // wait until squashfs-mount-rootless stops itself after mounting
        siginfo_t sig_info;
        if (waitid(P_PID, pid, &sig_info, WSTOPPED) < 0) {
            slurm_error("uenv: waitid failed: %s", strerror(errno));
            return -ESPANK_ERROR;
        }

        // enter the user+mount namespace created by squashfs-mount-rootless;
        // join_end will record winner_pid = getpid() so losing tasks can join
        // the same namespace
        uenv::namespaces_join(pid);
    } else {
        // winner_pid is the winner task's PID after it entered the squashfs
        // namespace, so joining its namespaces gives us the same view
        uenv::namespaces_join(join.shared->winner_pid);
    }

    uenv::join_end(join, ntasks, pid);

    return ESPANK_SUCCESS;
}

} // namespace impl
