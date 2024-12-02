#include <filesystem>
#include <string>
#include <vector>

#include <err.h>
#include <fcntl.h>
#include <sched.h>

#include <linux/loop.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/format.h>
#include <libmount/libmount.h>

#include <uenv/mount.h>
#include <util/expected.h>

namespace uenv {

util::expected<void, std::string> mount_entry::validate() const {
    namespace fs = std::filesystem;

    auto mount = fs::path(mount_path);

    // does the mount point exist?
    if (!fs::exists(mount)) {
        return util::unexpected(
            fmt::format("the mount point '{}' does not exist", mount_path));
    }

    mount = fs::canonical(mount);

    // is the mount point a directory?
    if (!fs::is_directory(mount)) {
        return util::unexpected(
            fmt::format("the mount point '{}' is not a directory", mount_path));
    }

    auto sqfs = fs::path(sqfs_path);

    // does the squashfs path exist?
    if (!fs::exists(sqfs)) {
        return util::unexpected(
            fmt::format("the squashfs file '{}' does not exist", sqfs_path));
    }

    // remove symlink etc, so that we can test file and permissions
    sqfs = fs::canonical(sqfs);

    // is the squashfs path a file ?
    if (!fs::is_regular_file(sqfs)) {
        return util::unexpected(
            fmt::format("the squashfs file '{}' is not a file", sqfs_path));
    }

    // do we have read access to the squashfs file?
    //
    // NOTE: this check is performed on the login node under the user's account.
    // The mount step is performed with elevated privelages on the compute node.
    // Skipping this check could possibly allow users to mount images to which
    // they don't have access.
    const fs::perms sqfs_perm = fs::status(sqfs).permissions();
    auto satisfies = [&sqfs_perm](fs::perms c) {
        return fs::perms::none != (sqfs_perm & c);
    };
    if (!(satisfies(fs::perms::owner_read) ||
          satisfies(fs::perms::group_read))) {
        return util::unexpected(
            fmt::format("you do not have read access to the squashfs file '{}'",
                        sqfs_path));
    }

    return {};
}

util::expected<void, std::string>
do_mount(const std::vector<mount_entry>& mount_entries) {
    if (mount_entries.size() == 0) {
        return {};
    }
    if (unshare(CLONE_NEWNS) != 0) {
        return util::unexpected("Failed to unshare the mount namespace");
    }
    // make all mounts in new namespace slave mounts, changes in the original
    // namesapce won't propagate into current namespace
    if (mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) != 0) {
        return util::unexpected(
            "mount: unable to change `/` to MS_SLAVE | MS_REC");
    }

    for (auto& entry : mount_entries) {
        std::string mount_point = entry.mount_path;
        std::string squashfs_file = entry.sqfs_path;

        if (!std::filesystem::is_regular_file(squashfs_file)) {
            return util::unexpected("the uenv squashfs file does not exist: " +
                                    squashfs_file);
        }
        if (!std::filesystem::is_directory(mount_point)) {
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
            const char* target_buf = mnt_context_get_target(cxt);
            // careful: mnt_context_get_target can return NULL
            std::string target = (target_buf == nullptr) ? "?" : target_buf;

            return util::unexpected(target + ": " + code_buf);
        }
    }

    return {};
}

} // namespace uenv
