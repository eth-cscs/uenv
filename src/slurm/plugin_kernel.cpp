#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include <spdlog/spdlog.h>

#include <uenv/log.h>
#include <uenv/mount.h>
#include <uenv/mount_kernel.h>
#include <uenv/parse.h>
#include <util/expected.h>
#include <util/privilege.h>

#include "environ.h"

extern "C" {
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
}

//
// Implementation
//
namespace impl {

namespace {

// The identity of the job user: uid, primary gid and supplementary groups, as
// reported by Slurm through spank_get_item.
//
// Returns an error if the uid or gid is unavailable. If Slurm does not report
// the supplementary groups, the primary gid is used as the only group: this can
// refuse an image that the user reads through another group, but never grants
// access that the user does not have.
util::expected<util::user_ids, std::string> job_ids(spank_t sp) {
    util::user_ids job;

    if (spank_get_item(sp, S_JOB_UID, &job.id.uid) != ESPANK_SUCCESS) {
        return util::unexpected("failed to get the job uid");
    }
    if (spank_get_item(sp, S_JOB_GID, &job.id.gid) != ESPANK_SUCCESS) {
        return util::unexpected("failed to get the job gid");
    }

    // S_JOB_SUPPLEMENTARY_GIDS has two out-parameters, and the array belongs
    // to Slurm, so copy it out.
    gid_t* gids = nullptr;
    int ngids = 0;
    if (spank_get_item(sp, S_JOB_SUPPLEMENTARY_GIDS, &gids, &ngids) ==
            ESPANK_SUCCESS &&
        gids != nullptr && ngids > 0) {
        job.groups.assign(gids, gids + ngids);
    } else {
        // Slurm does not resolve the group list when LaunchParameters=send_gids
        // is off.
        slurm_info("uenv: no supplementary groups for the job; access to the "
                   "uenv images will be checked against gid %u only",
                   job.id.gid);
        job.groups = {job.id.gid};
    }

    return job;
}

} // namespace

// Performs mounting of the squashfs images inside slurm_spank_init_post_opt in
// the _remote_ context. The squashfs images to mount and their mount points are
// set in the local and allocator contexts, where they are encoded in
// the environment variable UENV_MOUNT_LIST.
// This function relies on this variable being set.
//
// * parse UENV_MOUNT_LIST environment variable if set
// * check that each image:mountpoint is valid, and that the job user may read
//   the images, by opening them as the job user
// * perform mount, as root, on the descriptors that were opened above
int init_post_opt_remote(spank_t sp) {
    // initialise logging to be completely disabled
    uenv::init_log(spdlog::level::off);

    // parse environment variables to test whether there is anything to
    // mount
    auto mount_var = uenv::slurm::getenv_wrapper(sp, "UENV_MOUNT_LIST");

    // variable is not set - nothing to do here
    if (!mount_var) {
        return ESPANK_SUCCESS;
    }

    // The identity the images are opened with: the same uid, gid and groups
    // that Slurm assigns to the job's tasks. All three parts are required for
    // the access check. The supplementary groups grant access to images that
    // are readable through a project group, and installing them replaces the
    // groups of this process, which are root's.
    auto job = job_ids(sp);
    if (!job) {
        slurm_error("uenv: %s", job.error().c_str());
        return -ESPANK_ERROR;
    }

    // Validate the mount list and open the images as the job user.
    //
    // The descriptors opened here are bound to the loop devices below, so the
    // kernel's permission check at open(2), on the file mode and on directory
    // traversal, decides whether the user may mount the image, and the path is
    // never opened again with a different identity. Validation runs as the job
    // user for the same reason: every path this plugin examines, and every
    // error it reports, is limited to what the user could observe themselves.
    //
    // run_as_user confines the identity change to one thread, and slurmstepd
    // itself stays root.
    util::expected<std::vector<uenv::opened_image>, std::string> images =
        util::unexpected("the uenv images were not opened");
    if (auto result =
            util::run_as_user(job.value(),
                              [&images, &mount_var]() {
                                  auto mounts = uenv::parse_and_validate_mounts(
                                      mount_var.value());
                                  if (!mounts) {
                                      images = util::unexpected(mounts.error());
                                      return;
                                  }
                                  images = uenv::open_images(mounts.value());
                              });
        !result) {
        slurm_error("uenv: unable to open the uenv images as the job user: %s",
                    result.error().c_str());
        return -ESPANK_ERROR;
    }
    if (!images) {
        slurm_error("%s", images.error().c_str());
        return -ESPANK_ERROR;
    }

    if (auto result = uenv::unshare_mount_namespace(); !result) {
        slurm_error("%s", result.error().c_str());
        return -ESPANK_ERROR;
    }

    if (auto result = uenv::do_mount(images.value()); !result) {
        slurm_error("error mounting the requested uenv image: %s",
                    result.error().c_str());
        return -ESPANK_ERROR;
    }

    return ESPANK_SUCCESS;
}

} // namespace impl
