#include <string>

#include <err.h>
#include <fcntl.h>
#include <sched.h>

#include <linux/loop.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libmount/libmount.h>

#include "expected.hpp"
#include "filesystem.hpp"
#include "mount.hpp"

namespace util {

util::expected<std::string, std::string>
do_mount(const std::vector<mount_entry> &mount_entries) {
  if (mount_entries.size() == 0) {
    return "nothing to mount";
  }
  if (unshare(CLONE_NEWNS) != 0) {
    return util::unexpected("Failed to unshare the mount namespace");
  }
  // make all mounts in new namespace slave mounts, changes in the original
  // namesapce won't propagate into current namespace
  if (mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) != 0) {
    return util::unexpected("mount: unable to change `/` to MS_SLAVE | MS_REC");
  }

  for (auto &entry : mount_entries) {
    std::string mount_point = entry.mount_point;
    std::string squashfs_file = entry.image_path;

    if (!util::is_file(squashfs_file)) {
      return util::unexpected("the uenv squashfs file does not exist: " +
                              squashfs_file);
    }
    if (!util::is_valid_mountpoint(mount_point)) {
      return util::unexpected("the mount point is not a valide path: " +
                              mount_point);
    }

    auto cxt = mnt_new_context();

    if (mnt_context_disable_mtab(cxt, 1) != 0) {
      return util::unexpected("Failed to disable mtab");
    }

    if (mnt_context_set_fstype(cxt, "squashfs") != 0) {
      return util::unexpected("Failed to set fstype to squashfs");
    }

    if (mnt_context_append_options(cxt, "loop,nosuid,nodev,ro") != 0) {
      return util::unexpected("Failed to set mount options");
    }

    if (mnt_context_set_source(cxt, squashfs_file.c_str()) != 0) {
      return util::unexpected("Failed to set source");
    }

    if (mnt_context_set_target(cxt, mount_point.c_str()) != 0) {
      return util::unexpected("Failed to set target");
    }

    // https://ftp.ntu.edu.tw/pub/linux/utils/util-linux/v2.38/libmount-docs/libmount-Mount-context.html#mnt-context-mount
    const int rc = mnt_context_mount(cxt);
    const bool success = rc == 0 && mnt_context_get_status(cxt) == 1;
    if (!success) {
      char code_buf[256];
      mnt_context_get_excode(cxt, rc, code_buf, sizeof(code_buf));
      const char *target_buf = mnt_context_get_target(cxt);
      // careful: mnt_context_get_target can return NULL
      std::string target = (target_buf == nullptr) ? "?" : target_buf;

      return util::unexpected(target + ": " + code_buf);
    }
  }

  return "succesfully mounted";
}

} // namespace util
