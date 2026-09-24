#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <vector>

#include <sys/mount.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
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
// Note: the squashfs validation here is advisory - it produces a fast, clear
// error before anything is mounted. It is NOT security-relevant, because the
// path can change between this check and the mount. The authoritative check is
// performed on the exact fd that is bound to the loop device, in
// attach_loop_device().
// Both privileged kernel-backend callers (the setuid helper and the Slurm
// plugin) run this function with the calling user's credentials, so every path
// it touches, and every error message it produces, is limited to what that
// user could have discovered for themselves. Do not move it back above a
// privilege elevation: as root it becomes an existence/type/first-4-bytes
// oracle for arbitrary paths.
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

    // Distinguish "cannot look at it" from "is not a regular file". fs::status
    // reports EACCES through `ec` and then is_regular_file(status) is false, so
    // folding the two together would report a file the caller may not read - or
    // whose directory they may not traverse - as "is not a regular file". That
    // is now the ordinary outcome for an image the caller has no access to,
    // because these checks run with the caller's credentials, so the
    // diagnosis has to be accurate.
    const auto status = fs::status(sqfs, ec);
    if (ec) {
        return util::unexpected{fmt::format("unable to read squashfs {} ({})",
                                            sqfs.string(), ec.message())};
    }
    if (!fs::is_regular_file(status)) {
        return util::unexpected{fmt::format(
            "invalid squashfs {} (is not a regular file)", sqfs.string())};
    }

    // A valid squashfs file contains the magic string "hsqs" in the first 4
    // bytes. Use the error_code overload: the throwing one would terminate the
    // setuid helper, which has no handler, if the file went away or became
    // unreadable after the check above.
    if (fs::file_size(sqfs, ec) < 4 || ec) {
        return util::unexpected{fmt::format(
            "unable to read squashfs {} (not a valid squashfs file)",
            sqfs.string())};
    }
    // Note that a file the caller has no access to gets this far: its metadata
    // is readable whenever the containing directory can be traversed, so it is
    // the open below - not the checks above - that reports the refusal. Say
    // why it failed: "unable to read" on its own sends users looking for a
    // corrupt image rather than a permission problem.
    errno = 0;
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
        const int open_errno = errno;
        return util::unexpected{fmt::format(
            "unable to read squashfs {}{}", sqfs.string(),
            open_errno ? fmt::format(" ({})", strerror(open_errno)) : "")};
    }

    return mount_pair{.sqfs = sqfs, .mount = mount};
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

    // Check that a mount point is an existing directory. The throwing
    // overloads of std::filesystem must not be used here: a mount point
    // that cannot be examined (EACCES on a parent, ENAMETOOLONG, a stale
    // network file system) would terminate the process.
    auto check_mount_point =
        [](const fs::path& p) -> util::expected<void, std::string> {
        std::error_code ec;
        const auto status = fs::status(p, ec);
        if (ec) {
            return util::unexpected{
                fmt::format("unable to access the mount path {} ({})",
                            p.string(), ec.message())};
        }
        if (status.type() == fs::file_type::not_found) {
            return util::unexpected{
                fmt::format("the mount path {} does not exist", p.string())};
        }
        if (!fs::is_directory(status)) {
            return util::unexpected{fmt::format(
                "the mount path {} is not a directory", p.string())};
        }
        return {};
    };

    // check whether the first mount point exists
    if (auto ok = check_mount_point(*b); !ok) {
        return util::unexpected{ok.error()};
    }
    // iterate over the remaining mounts
    for (auto c = b + 1; c != e; ++c) {
        auto parent = std::find_if(
            b, c, [&c, is_child](const auto& it) { return is_child(it, *c); });
        if (parent == c) { // there is no parent
            if (auto ok = check_mount_point(*c); !ok) {
                return util::unexpected{ok.error()};
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
            return util::unexpected{
                fmt::format("invalid squashfs mount {}:{} - {}", desc.sqfs_path,
                            desc.mount_path, mount.error())};
        } else {
            mounts.push_back(mount.value());
        }
    }

    return validate_mount_list(mounts);
}

util::expected<mount_list, std::string>
parse_and_validate_mounts(const std::string& description) {
    auto mount_descriptions = uenv::parse_mount_list(description);
    if (!mount_descriptions) {
        return util::unexpected{mount_descriptions.error().message()};
    }

    return validate_mount_descriptions(mount_descriptions.value());
}

util::expected<void, std::string> mount(std::optional<std::string> source,
                                        const std::string& dest,
                                        std::optional<std::string> fstype,
                                        unsigned long mountflags,
                                        const void* nullable_data) {
    spdlog::trace("mount({}, {}, {}, {:b})",
                  source ? source.value().c_str() : "null", dest,
                  fstype ? fstype.value().c_str() : "null", mountflags);
    if (::mount(source ? source->c_str() : nullptr, dest.c_str(),
                fstype ? fstype->c_str() : nullptr, mountflags,
                nullable_data) != 0) {
        return util::unexpected(
            fmt::format("mount failed: {}", strerror(errno)));
    }
    return {};
}

} // namespace uenv
