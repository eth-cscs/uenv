#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
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

// A mount_description has string descriptions of the squashfs file path and
// mount path taken from parsing a CLI argument or environment variable.
// Convert this to a mount_pair by converting these to std::filesystem::path,
// and validating that the squashfs file exists and can be read.
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
        return util::unexpected{fmt::format("invalid squashfs {} ({})",
                                            d.mount_path, ec.message())};
    }

    if (!fs::is_regular_file(sqfs, ec)) {
        return util::unexpected{fmt::format(
            "invalid squashfs {} (is not a regular file)", sqfs.string())};
    }

    // A valid squashfs file contains the magic string "hsqs" in the first 4
    // bytes.
    if (std::filesystem::file_size(sqfs) < 4) {
        return util::unexpected{fmt::format(
            "unable to read squashfs {} (not a valid squashfs file)",
            sqfs.string())};
    }
    if (auto file = std::ifstream{sqfs, std::ios::binary}) {
        std::array<char, 4> magic{};
        file.read(reinterpret_cast<char*>(magic.data()), magic.size());

        // Compare against little-endian 'hsqs'
        if (!(magic[0] == 'h' && magic[1] == 's' && magic[2] == 'q' &&
              magic[3] == 's')) {
            return util::unexpected{fmt::format(
                "unable to read squashfs {} (not a valid squashfs file)",
                sqfs.string())};
        }
    } else {
        return util::unexpected{
            fmt::format("unable to read squashfs {}", sqfs.string())};
    }

    return mount_pair{.sqfs = sqfs, .mount = mount};
}

util::expected<std::vector<uenv::mount_pair>, std::string>
validate_mount_list(const mount_list& input, bool mount_points_must_exist) {
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
    // starting to parse squashfs files and their meta data (which we might
    // be forced to do at some point).

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

    const auto b = std::begin(mview);
    const auto e = std::cend(mview);

    // check whether the first mount point exists
    if (!fs::is_directory(*b) && mount_points_must_exist) {
        return util::unexpected{
            fmt::format("the mount path {} does not exist", (*b).string())};
    }
    // iterate over the remaining mounts
    for (auto c = b + 1; c != e; ++c) {
        auto parent = std::find_if(
            b, c, [&c, is_child](const auto& it) { return is_child(it, *c); });
        if (parent == c) { // there is no parent
            if (!fs::is_directory(*c) && mount_points_must_exist) {
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
validate_mount_descriptions(const std::vector<mount_description>& input,
                            bool mount_points_must_exist) {
    mount_list mounts;
    for (auto desc : input) {
        if (auto mount = uenv::make_mount_pair(desc); !mount) {
            return util::unexpected{
                fmt::format("invalid squashfs mount {}:{} - {}", desc.sqfs_path,
                            desc.mount_path, mount.error())};
        } else {
            mounts.push_back(mount.value());
        }
    }

    return validate_mount_list(mounts, mount_points_must_exist);
}

util::expected<mount_list, std::string>
parse_and_validate_mounts(const std::string& description,
                          bool mount_points_must_exist) {
    auto mount_descriptions = uenv::parse_mount_list(description);
    if (!mount_descriptions) {
        return util::unexpected{mount_descriptions.error().message()};
    }

    return validate_mount_descriptions(mount_descriptions.value(),
                                       mount_points_must_exist);
}

util::expected<void, std::string>
do_mount(const std::vector<mount_pair>& mount_entries) {
    if (mount_entries.size() == 0) {
        return {};
    }

    for (auto& entry : mount_entries) {
        std::string mount_point = entry.mount;
        std::string squashfs_file = entry.sqfs;

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

util::expected<void, std::string> mount(std::optional<std::string> source,
                                        const std::string& dest,
                                        std::optional<std::string> fstype,
                                        unsigned long mountflags,
                                        const void* nullable_data) {
    spdlog::trace("mount({}, {}, {}, {:b})",
                  source ? source.value().c_str() : "null", dest,
                  fstype ? fstype.value().c_str() : "null", mountflags);
    // spdlog::debug("foo bar {}", "foo");
    if (::mount(source ? source->c_str() : nullptr, dest.c_str(),
                fstype ? fstype->c_str() : nullptr, mountflags,
                nullable_data) != 0) {
        return util::unexpected(
            fmt::format("mount failed: {}", strerror(errno)));
    }
    return {};
}

util::expected<void, std::string>
mount_tmpfs(std::filesystem::path dst, std::optional<std::uint64_t> size) {
    std::string options = "mode=0755";
    if (size) {
        options = fmt::format("{},size={}", options, size.value());
    }
    return uenv::mount("tmpfs", dst, "tmpfs", MS_NOSUID | MS_NODEV,
                       options.c_str());
}

util::expected<void, std::string> bind_mount(std::filesystem::path src,
                                             std::filesystem::path dst) {
    spdlog::trace("bind_mount({}, {})", src.string(), dst.string());
    return uenv::mount(src, dst, std::nullopt, MS_BIND | MS_REC | MS_SILENT,
                       nullptr);
}

} // namespace uenv
