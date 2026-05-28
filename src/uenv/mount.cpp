#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <ranges>
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
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <libmount/libmount.h>
#include <spdlog/spdlog.h>

#include <uenv/mount.h>
#include <uenv/parse.h>
#include <util/expected.h>

namespace uenv {

void close_sqfs_fds(mount_list& mounts) noexcept {
    for (auto& entry : mounts) {
        if (entry.sqfs_fd) {
            close(*entry.sqfs_fd);
            entry.sqfs_fd = std::nullopt;
        }
    }
}

// A mount_description has string descriptions of the squashfs file path and
// mount path taken from parsing a CLI argument or environment variable.
// Convert this to a mount_pair by converting these to std::filesystem::path,
// validating that the squashfs file exists, and opening an fd that the caller
// can pass to do_mount (so NFS root_squash does not block the loop mount).
// The existance of the mount points is not checked, because these need to be
// checked when mounting.
util::expected<mount_pair, std::string>
make_mount_pair(const mount_description& d) {
    namespace fs = std::filesystem;

    std::error_code ec{};
    const auto mount = fs::weakly_canonical(fs::path(d.mount_path), ec);
    if (ec) {
        return util::unexpected{fmt::format("invalid mount point {} ({})",
                                            d.mount_path, ec.message())};
    }

    const auto sqfs = fs::weakly_canonical(fs::path(d.sqfs_path), ec);
    if (ec) {
        return util::unexpected{
            fmt::format("invalid squashfs {} ({})", d.sqfs_path, ec.message())};
    }

    if (!fs::is_regular_file(sqfs, ec)) {
        return util::unexpected{fmt::format(
            "invalid squashfs {} (is not a regular file)", sqfs.string())};
    }

    // Open the file and verify the squashfs magic bytes ("hsqs").
    // The fd is kept open and stored in the returned mount_pair so that
    // do_mount can use /proc/self/fd/N as the loop source, bypassing NFS
    // permission checks that would otherwise fire under euid=0 with
    // root_squash.
    const int fd = open(sqfs.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return util::unexpected{fmt::format("unable to read squashfs {} ({})",
                                            sqfs.string(), strerror(errno))};
    }

    std::array<char, 4> magic{};
    if (read(fd, magic.data(), magic.size()) != 4 ||
        !(magic[0] == 'h' && magic[1] == 's' && magic[2] == 'q' &&
          magic[3] == 's')) {
        close(fd);
        return util::unexpected{fmt::format(
            "unable to read squashfs {} (not a valid squashfs file)",
            sqfs.string())};
    }

    return mount_pair{.sqfs = sqfs, .mount = mount, .sqfs_fd = fd};
}

util::expected<std::vector<uenv::mount_pair>, std::string>
validate_mount_list(const mount_list& input) {
    namespace fs = std::filesystem;

    if (input.empty()) {
        return input;
    }

    // Iterate over the mount points and verify whether they exist.
    // There is a wrinkle: a mount point may be "inside" another mount
    // point, and thus rely on the other mount first being mounted before it
    // can be mounted. We first sort the mount entries so that they can be
    // mounted like this, and only check for existance of "root" mounts that
    // are not inside another mount.
    //
    // Note that we do not check for the existance of mount points that have
    // to be provided by another mount. This would be very tricky, without
    // starting to parse squashfs files and their meta data - WHICH WE DO NOT
    // WANT TO DO BECAUSE THIS CODE RUNS IN THE PRIVILAGED HELPER.

    std::vector<uenv::mount_pair> mounts{input};
    std::sort(std::begin(mounts), std::end(mounts),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.mount.compare(rhs.mount) < 0;
              });

    auto is_child = [](const fs::path& parent, const fs::path& child) -> bool {
        auto rel = child.lexically_relative(parent);
        return !rel.empty() && *rel.begin() != "..";
    };

    auto mview = std::ranges::transform_view(
        mounts, [](const auto& in) -> const fs::path& { return in.mount; });

    // check whether there are two identical mount points.
    // take advantage of the list being sorted.
    if (auto it = std::adjacent_find(mview.begin(), mview.end());
        it != mview.end()) {
        return util::unexpected{fmt::format(
            "the mount point {} is used to mount more than one squashfs", *it)};
    }

    const auto b = std::begin(mview);
    const auto e = std::cend(mview);

    // check whether the first mount point exists
    if (!fs::is_directory(*b)) {
        return util::unexpected{
            fmt::format("the mount path {} does not exist", (*b).string())};
    }
    // iterate over the remaining mounts
    for (auto c = b + 1; c != e; ++c) {
        auto parent = std::find_if(
            b, c, [&c, is_child](const auto& it) { return is_child(it, *c); });
        if (parent == c) { // there is no parent
            if (!fs::is_directory(*c)) {
                return util::unexpected{
                    fmt::format("the mount path {} does not exist", *c)};
            }
        } else {
            spdlog::warn("the mount {} is inside another mount {}", *c,
                         *(c - 1));
        }
    }

    return mounts;
}

util::expected<mount_list, std::string>
validate_mount_descriptions(const std::vector<mount_description>& input) {
    mount_list mounts;
    for (auto desc : input) {
        if (auto mount = uenv::make_mount_pair(desc); !mount) {
            close_sqfs_fds(mounts);
            return util::unexpected{
                fmt::format("invalid squashfs mount {}:{} - {}", desc.sqfs_path,
                            desc.mount_path, mount.error())};
        } else {
            mounts.push_back(mount.value());
        }
    }

    auto result = validate_mount_list(mounts);
    if (!result) {
        close_sqfs_fds(mounts);
    }
    return result;
}

util::expected<mount_list, std::string>
parse_and_validate_mounts(const std::string& description) {
    auto mount_descriptions = uenv::parse_mount_list(description);
    if (!mount_descriptions) {
        return util::unexpected{mount_descriptions.error().message()};
    }

    return validate_mount_descriptions(mount_descriptions.value());
}

util::expected<void, std::string>
do_mount(const std::vector<mount_pair>& mount_entries) {
    if (mount_entries.size() == 0) {
        return {};
    }

    for (const auto& entry : mount_entries) {
        std::string mount_point = entry.mount;
        // Use /proc/self/fd/N when a pre-opened fd is available so the kernel
        // does not re-check NFS permissions under the elevated euid=0.
        const std::string squashfs_file =
            entry.sqfs_fd ? fmt::format("/proc/self/fd/{}", *entry.sqfs_fd)
                          : entry.sqfs.string();

        // Check the mount point exists inside the mount loop, because the
        // mount point may have been created inside a previous mount.
        if (!std::filesystem::is_directory(mount_point)) {
            return util::unexpected("the mount point is not a valid path: " +
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
