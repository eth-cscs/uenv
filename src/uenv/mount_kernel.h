#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <sys/types.h>

#include <fmt/core.h>

#include <uenv/mount.h>
#include <util/expected.h>
#include <util/unique_fd.h>

namespace uenv {

/// Move into a private mount namespace and make every mount in it a slave, so
/// that mounts made here never propagate back to the parent namespace.
/// Requires CAP_SYS_ADMIN: the caller must already be root.
util::expected<void, std::string> unshare_mount_namespace();

/// An image opened with the calling user's credentials, paired with the place
/// it is to be mounted.
///
/// Generated in open_images, using the credentials of the user, then passed to
/// do_mount for mounting as root. The file descriptor is not closed between
/// being opened and mounted, ensuring that only files that the user has access
/// to can be mounted.
struct opened_image {
    util::unique_fd fd;
    /// the canonical path, used for diagnostics and lo_file_name only - never
    /// re-opened, because re-opening it would re-introduce the question of
    /// whose credentials the open is made with.
    std::filesystem::path sqfs;
    std::filesystem::path mount;
};

/// Open every image in `mount_entries` with the credentials currently in
/// effect, preserving the input order (validate_mount_list has already sorted
/// it so that parent mounts precede the mounts nested inside them).
///
/// Refuses to run when the effective user is root but the real user is not, to
/// avoid opening a caller-named image with privileges the caller does not have.
util::expected<std::vector<opened_image>, std::string>
open_images(const mount_list& mount_entries);

/// mount images that have already been opened by open_images().
/// NOTE: the mount namespace must have been unshared before calling this
/// function.
util::expected<void, std::string>
do_mount(const std::vector<opened_image>& images);

} // namespace uenv
