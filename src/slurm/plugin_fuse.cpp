#include "environ.h"
#include <fmt/ranges.h>
#include <optional>
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
#include <spdlog/spdlog.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <uenv/log.h>
#include <uenv/rootless.h>
#include <util/proc_barrier.h>
#include <util/setns.h>

//
// Forward declare the implementation of the plugin callbacks.
//
namespace impl {
int slurm_spank_task_init_sqfs_ll(spank_t sp);
} // namespace impl

extern "C" {

int slurm_spank_task_init(spank_t sp, [[maybe_unused]] int ac,
                          [[maybe_unused]] char** av) {
    return impl::slurm_spank_task_init_sqfs_ll(sp);
}

} // extern "C"

namespace impl {

// The FUSE backend mounts per task in slurm_spank_task_init, so there is
// nothing to do in the remote context. This no-op satisfies the shared
// dispatcher in plugin.cpp (the kernel backend provides the real
// implementation in plugin_kernel.cpp).
int init_post_opt_remote(spank_t) {
    return ESPANK_SUCCESS;
}

// use the squashfuse_ll interface
int slurm_spank_task_init_sqfs_ll(spank_t sp) {
    uenv::init_log(spdlog::level::off);

    // parse environment variables to test whether there is anything to
    // mount
    auto mount_var = uenv::slurm::getenv_wrapper(sp, "UENV_MOUNT_LIST");

    // variable is not set - nothing to do here
    if (!mount_var)
        return ESPANK_SUCCESS;

    const uid_t uid = getuid();
    const uid_t gid = getgid();

    int ntasks = 1;
    uint32_t job_id = 0;
    uint32_t step_id = 0;
    spank_get_item(sp, S_JOB_LOCAL_TASK_COUNT, &ntasks);
    spank_get_item(sp, S_JOB_ID, &job_id);
    spank_get_item(sp, S_JOB_STEPID, &step_id);
    const auto barrier_tag = fmt::format("{}-{}", job_id, step_id);

    // parse and validate the mount descriptions
    // note that it is very important to carefully validate the mount_list
    // * check that the squashfs files exist and can be read by the user
    // * check that the mount points exist
    auto mounts = uenv::parse_and_validate_mounts(mount_var.value());
    if (!mounts) {
        slurm_error("%s", mounts.error().c_str());
        return -ESPANK_ERROR;
    }

    auto barrier = util::proc_barrier::create(barrier_tag, ntasks);
    if (!barrier) {
        slurm_error("%s", barrier.error().c_str());
        return -ESPANK_ERROR;
    }

    if (barrier->is_leader()) {
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

        // exit fake-root. Note: this does its own unshare(CLONE_NEWUSER |
        // CLONE_NEWNS), so it moves us into a *new* mount namespace (a copy
        // of the current one) -- the other tasks must join this final
        // namespace, not the one from before this call, so the release below
        // must come after it.
        if (auto r = uenv::rootless::map_effective_user(uid, gid); !r) {
            slurm_error("failed map effective user %s %d %d", r.error().c_str(),
                        uid, gid);
            return -ESPANK_ERROR;
        }

        // publish our pid and release the tasks blocked in the barrier so
        // they can join our (now final) namespaces *before* we go
        // non-dumpable below.
        if (auto r = barrier->ready(); !r) {
            slurm_error("barrier ready failed: %s", r.error().c_str());
            return -ESPANK_ERROR;
        }

        // wait until every other local task has finished joining our
        // namespaces -- only then is it safe to flip DUMPABLE off.
        if (auto r = barrier->wait_peers(); !r) {
            slurm_error("barrier wait_peers failed: %s", r.error().c_str());
            return -ESPANK_ERROR;
        }

        // safe now: every peer has already opened /proc/<our pid>/ns/*.
        if (prctl(PR_SET_DUMPABLE, 0) != 0) {
            slurm_error("PR_SET_DUMPABLE failed");
            return -ESPANK_ERROR;
        }

        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            slurm_error("PR_SET_NO_NEW_PRIVS failed");
            return -ESPANK_ERROR;
        }
    } else {
        // the leader publishes the PID that entered the squashfs namespace,
        // so joining its namespaces gives us the same view
        auto r = util::namespaces_join(barrier->leader_pid(), {"user", "mnt"});

        // signal the leader regardless of outcome, so it isn't left waiting
        // out the full barrier timeout for a peer that will never succeed.
        if (auto sr = barrier->signal_done(); !sr) {
            slurm_error("barrier signal_done failed: %s", sr.error().c_str());
        }
        if (!r) {
            slurm_error("namespaces_join failed: %s", r.error().c_str());
            return -ESPANK_ERROR;
        }
    }

    if (auto r = barrier->end(); !r) {
        slurm_error("barrier end failed %s", r.error().c_str());
        return -ESPANK_ERROR;
    }

    return ESPANK_SUCCESS;
}
} // namespace impl
