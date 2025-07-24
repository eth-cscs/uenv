#include <slurm/mount_slurm.h>

#include <linux/loop.h>
#include <sched.h>
#include <string>
#include <sys/mount.h>
#include <util/expected.h>

namespace uenv {

/// non-setuid version
util::expected<void, std::string> unshare_as_root() {
    if (unshare(CLONE_NEWNS) != 0) {
        return util::unexpected("Failed to unshare the mount namespace");
    }
    // make all mounts in new namespace slave mounts, changes in the
    // original namesapce won't propagate into current namespace
    if (mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) != 0) {
        return util::unexpected(
            "mount: unable to change `/` to MS_SLAVE | MS_REC");
    }
    return {};
}

} // namespace uenv
