#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sched.h>

#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/mount.h>
#include <uenv/mount_kernel.h>
#include <uenv/parse.h>
#include <util/defer.h>
#include <util/expected.h>
#include <util/privilege.h>

namespace uenv {

util::expected<void, std::string> unshare_mount_namespace() {
    // unshare(CLONE_NEWNS) and the MS_SLAVE remount both need CAP_SYS_ADMIN in
    // the effective set, which only an effective uid of 0 provides
    // (capabilities(7), "Effect of user ID changes").
    if (unshare(CLONE_NEWNS) != 0) {
        return util::unexpected(fmt::format(
            "failed to unshare the mount namespace: {}", strerror(errno)));
    }

    if (auto r = uenv::mount(std::nullopt, "/", std::nullopt, MS_SLAVE | MS_REC,
                             nullptr);
        !r) {
        return r;
    }

    return {};
}

//
// open_images
//

util::expected<std::vector<opened_image>, std::string>
open_images(const mount_list& mount_entries) {
    // Precondition: the calling thread has the identity of the user whose
    // images these are. An effective uid of 0 with a different real uid means
    // that privilege has not been dropped, and opening the images would bypass
    // the user's access check.
    auto ids = util::current_ids();
    if (!ids) {
        return util::unexpected(ids.error());
    }
    if (ids->effective.uid == 0 && ids->real.uid != 0) {
        return util::unexpected(
            "internal error: refusing to open uenv images as root on behalf of "
            "another user");
    }

    std::vector<opened_image> images;
    images.reserve(mount_entries.size());

    for (auto& entry : mount_entries) {
        // O_NOFOLLOW rejects a final-component symlink swap. entry.sqfs is the
        // already-canonicalized path (see make_mount_pair), so under honest
        // use the final component is a regular file and O_NOFOLLOW never
        // triggers. This guarantees that the open is made with the
        // caller's credentials, a symlink the caller can create points only
        // at things the caller could have named directly, but it costs
        // nothing and mirrors util-linux's loopdev symlink-attack fix
        // (LOOPDEV_FL_NOFOLLOW, advisory GHSA-qq4x-vfq4-9h9g).
        util::unique_fd fd{
            open(entry.sqfs.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
        if (!fd) {
            return util::unexpected(fmt::format("unable to open the uenv image "
                                                "{}: {}",
                                                entry.sqfs, strerror(errno)));
        }
        spdlog::debug("open_images: opened {} as uid {}", entry.sqfs,
                      ids->real.uid);
        images.push_back(opened_image{
            .fd = std::move(fd), .sqfs = entry.sqfs, .mount = entry.mount});
    }

    return images;
}

namespace {

// Retry an ioctl while it returns EAGAIN: udev and blkid briefly open loop
// devices when their state changes, and the kernel returns EAGAIN while such
// a transient opener holds the device.
// Mirrors util-linux repeat_on_eagain (lib/loopdev.c): 10 tries at 250ms.
template <typename F> int retry_on_eagain(F&& f) {
    constexpr int max_tries = 10;
    int rc = 0;
    for (int i = 0; i <= max_tries; ++i) {
        errno = 0;
        rc = f();
        if (rc == 0 || errno != EAGAIN) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return rc;
}

struct loop_device {
    std::string path;
    // A read-only fd that keeps the device attached until it is mounted:
    // LO_FLAGS_AUTOCLEAR detaches the device as soon as the last reference
    // (open fd or mount) is dropped.
    int fd;
};

// Attach a squashfs image, already opened read-only, to a free loop device.
//
// The descriptor is supplied by the caller rather than opened here:
// open_images() opens the image with the *calling user's* credentials, and this
// function then binds that descriptor with LOOP_CONFIGURE while running as
// root. DAC is checked at open(2) only, so the mount inherits the caller's
// authority over the image and no privileged re-open of a caller-named path
// ever happens. `display_name` is the canonical path, used for lo_file_name and
// diagnostics.
//
// We use direct loop ioctls + mount(2) instead of libmount's context API
// because libmount >= 2.42 marks any setuid process as "restricted" (via
// AT_SECURE / is_privileged_execution) and refuses to mount without a
// matching /etc/fstab entry, even after setreuid(0,0).
//
// The acquire sequence is retried on EBUSY: LOOP_CTL_GET_FREE does not
// reserve the device it returns, and the loop device pool is global to the
// node, so a concurrent process (another job step or uenv invocation) can
// claim the device before our LOOP_CONFIGURE.
util::expected<loop_device, std::string>
attach_loop_device(const int sqfs_fd, const std::string& squashfs_file) {
    // Validate the image on the fd we are about to bind.
    // Binding the fd we validated, rather than re-opening the path, closes the
    // time-of-check/time-of-use gap that would otherwise let the path be
    // swapped between validation and bind.
    // make_mount_pair performs the same checks earlier by path to give
    // user-friendly error messages for accidental user errors.
    // This check ensures that nothing nefarious is going on, and is the
    // authoritative validation of a caller-provided fd.
    struct stat st = {};
    if (fstat(sqfs_fd, &st) != 0) {
        return util::unexpected(
            fmt::format("stat {}: {}", squashfs_file, strerror(errno)));
    }
    if (!S_ISREG(st.st_mode)) {
        return util::unexpected(
            fmt::format("{} is not a regular file", squashfs_file));
    }
    // A valid squashfs file starts with the little-endian magic "hsqs".
    std::array<char, 4> magic = {};
    if (pread(sqfs_fd, magic.data(), magic.size(), 0) !=
            static_cast<ssize_t>(magic.size()) ||
        !(magic[0] == 'h' && magic[1] == 's' && magic[2] == 'q' &&
          magic[3] == 's')) {
        return util::unexpected(
            fmt::format("{} is not a valid squashfs file", squashfs_file));
    }

    constexpr int max_acquire_attempts = 16;
    for (int attempt = 0; attempt < max_acquire_attempts; ++attempt) {
        int ctrl_fd = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
        if (ctrl_fd < 0) {
            return util::unexpected(
                fmt::format("open /dev/loop-control: {}", strerror(errno)));
        }
        const int loopnum = ioctl(ctrl_fd, LOOP_CTL_GET_FREE);
        const int get_free_errno = errno;
        close(ctrl_fd);
        if (loopnum < 0) {
            return util::unexpected(
                fmt::format("LOOP_CTL_GET_FREE: {}", strerror(get_free_errno)));
        }

        const std::string loopdev = fmt::format("/dev/loop{}", loopnum);

        // When LOOP_CTL_GET_FREE allocates a new device the /dev/loopN node
        // is created asynchronously by udev, so wait out ENOENT (node not
        // yet created) and EACCES (permissions not yet applied).
        // The device is opened O_RDWR, as required by LOOP_CONFIGURE.
        int loop_fd = -1;
        for (int i = 0; i < 16; ++i) {
            loop_fd = open(loopdev.c_str(), O_RDWR | O_CLOEXEC);
            if (loop_fd >= 0 || (errno != ENOENT && errno != EACCES)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (loop_fd < 0) {
            return util::unexpected(
                fmt::format("open {}: {}", loopdev, strerror(errno)));
        }
        auto close_loop = util::defer([loop_fd] { close(loop_fd); });

        // Bind the backing file and set the flags in one atomic
        // LOOP_CONFIGURE (Linux >= 5.8):
        // - LO_FLAGS_READ_ONLY makes the block device read-only (also
        //   implied by the O_RDONLY backing fd);
        // - LO_FLAGS_AUTOCLEAR detaches the device automatically once the
        //   last reference (mount or open fd) is gone, so no explicit
        //   LOOP_CLR_FD cleanup is needed on any path after this point.
        struct loop_config config = {};
        config.fd = static_cast<unsigned int>(sqfs_fd);
        config.info.lo_flags = LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR;
        strncpy(reinterpret_cast<char*>(config.info.lo_file_name),
                squashfs_file.c_str(), LO_NAME_SIZE - 1);

        if (retry_on_eagain(
                [&] { return ioctl(loop_fd, LOOP_CONFIGURE, &config); }) != 0) {
            if (errno == EBUSY) {
                // the device was claimed by another process between
                // LOOP_CTL_GET_FREE and LOOP_CONFIGURE - try the next one
                spdlog::debug("attach_loop_device: {} stolen, retrying",
                              loopdev);
                continue;
            }
            return util::unexpected(fmt::format("{}: LOOP_CONFIGURE: {}",
                                                loopdev, strerror(errno)));
        }

        // Swap the setup fd for a read-only one before mounting: the kernel
        // blocks mounting a block device that has writable openers.
        const int ro_fd = open(loopdev.c_str(), O_RDONLY | O_CLOEXEC);
        if (ro_fd < 0) {
            // closing loop_fd (close_loop) drops the last reference and
            // AUTOCLEAR detaches the device
            return util::unexpected(
                fmt::format("open {} read-only: {}", loopdev, strerror(errno)));
        }
        return loop_device{loopdev, ro_fd};
    }

    return util::unexpected(fmt::format(
        "unable to acquire a free loop device for {} after {} attempts",
        squashfs_file, max_acquire_attempts));
}

} // namespace

util::expected<void, std::string>
do_mount(const std::vector<opened_image>& images) {
    if (images.size() == 0) {
        return {};
    }

    for (auto& entry : images) {
        std::string mount_point = entry.mount;
        std::string squashfs_file = entry.sqfs;

        // Check the mount point exists inside the mount loop, because the
        // mount point may have been created inside a previous mount.
        if (!std::filesystem::is_directory(mount_point)) {
            return util::unexpected("the mount point is not a valid path: " +
                                    mount_point);
        }

        auto loop = attach_loop_device(entry.fd.get(), squashfs_file);
        if (!loop) {
            return util::unexpected(
                fmt::format("{}: {}", mount_point, loop.error()));
        }
        // The mount takes its own reference to the loop device, after which
        // the fd is no longer needed; if the mount fails, closing the fd
        // drops the last reference and AUTOCLEAR detaches the device.
        auto close_loop = util::defer([fd = loop->fd] { close(fd); });

        if (::mount(loop->path.c_str(), mount_point.c_str(), "squashfs",
                    MS_RDONLY | MS_NOSUID | MS_NODEV, nullptr) != 0) {
            const int saved_errno = errno;
            const char* hint =
                saved_errno == EINVAL
                    ? " (bad superblock - corrupt or truncated squashfs?)"
                    : "";
            return util::unexpected(fmt::format(
                "failed to mount {} at {} via {}: {}{}", squashfs_file,
                mount_point, loop->path, strerror(saved_errno), hint));
        }
    }

    return {};
}

} // namespace uenv
