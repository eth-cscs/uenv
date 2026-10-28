#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

#include <fmt/core.h>

#include <util/expected.h>

namespace uenv {

struct mount_description {
    std::string sqfs_path;
    std::string mount_path;
};

struct mount_pair {
    std::filesystem::path sqfs;
    std::filesystem::path mount;
};

struct tmpfs_tuple {
    std::filesystem::path mount;
    std::optional<std::uint64_t> size;
};

struct bindmount_pair {
    std::filesystem::path src;
    std::filesystem::path dst;
};

// convert a description to a mount_pair that has a validated squashfs path
util::expected<mount_pair, std::string>
make_mount_pair(const mount_description& description);

using mount_list = std::vector<mount_pair>;

// true if `child` is `parent` itself, or lies anywhere below it, decided by
// path component (not string prefix) comparison.
bool is_nested_path(const std::filesystem::path& parent,
                    const std::filesystem::path& child);

// Which CLI flag a mount_order_entry's payload came from.
enum class mount_kind { sqfs, bind_mount, tmpfs };

struct mount_order_entry {
    mount_kind kind;
    // index into the mounts, bind_mounts or tmpfs vector, per `kind`
    std::size_t index;
};
using mount_order = std::vector<mount_order_entry>;

// Order --sqfs, --bind-mount and --tmpfs entries by a lexical, stable sort
// of their destination paths: a parent directory's path always sorts before
// anything nested under it, so sorting this way mounts parents before
// children, regardless of which flag was used for which or what order they
// were given on the command line. A --bind-mount or --tmpfs can therefore be
// placed inside a squashfs image, and a squashfs image inside a --tmpfs. Two
// entries with an identical destination are rejected as an error (mirrors
// the duplicate mount-point check in validate_mount_list for --sqfs) rather
// than picked between.
util::expected<mount_order, std::string>
build_mount_order(const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_tuple>& tmpfs);

// one shot from string description to sorted and validated inputs
//
// auto mountvar ;
// if (auto mountvar = env.get("UENV_MOUNT_LIST")) {
//     auto mounts = parse_and_validate_mounts(mountvar.value());
// }
util::expected<mount_list, std::string>
parse_and_validate_mounts(const std::string& description,
                          bool mount_points_must_exist = true);

/// wrapper to `mount` from `sys/mount.h`
util::expected<void, std::string> mount(std::optional<std::string> source,
                                        const std::string& dest,
                                        std::optional<std::string> fstype,
                                        unsigned long mountflags,
                                        const void* nullable_data);

/// create a tmpfs
util::expected<void, std::string>
mount_tmpfs(std::filesystem::path dst, std::optional<std::uint64_t> size);

/// bind mount
util::expected<void, std::string> bind_mount(std::filesystem::path src,
                                             std::filesystem::path dst);

} // namespace uenv

template <> class fmt::formatter<uenv::mount_pair> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::mount_pair const& r, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}:{}", r.sqfs.string(),
                              r.mount.string());
    }
};
