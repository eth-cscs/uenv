#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <sys/types.h>
#include <utility>
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

// one line of /proc/self/mountinfo (see proc_pid_mountinfo(5)): only the
// fields that uenv uses are kept.
struct mountinfo_entry {
    std::uint32_t id{};
    std::uint32_t parent_id{};
    // with the kernel's octal escapes (\040 for a space, ...) decoded
    std::filesystem::path mountpoint;
    // the per-mount options, e.g. "rw,nosuid,relatime"
    std::string vfs_options;
    std::string fstype;
};

// convert a description to a mount_pair that has a validated squashfs path
util::expected<mount_pair, std::string>
make_mount_pair(const mount_description& description);

using mount_list = std::vector<mount_pair>;

// The one spelling every --sqfs, --tmpfs and --bind-mount path is compared in:
// absolute (a relative path is resolved against the working directory),
// lexically normal, with symlinks in the part that exists resolved, and
// without a trailing separator. The mount order and its duplicate check
// (build_mount_order) rely on every path having been through this.
util::expected<std::filesystem::path, std::string>
canonical_mount_path(const std::filesystem::path& p);

// --tmpfs mount points, and --bind-mount sources and destinations, as parsed
// by parse_tmpfs_list and parse_bindmount_list, in canonical form (see
// canonical_mount_path), the same as --sqfs mount points (make_mount_pair).
// Whether they exist is checked by validate_mount_points, once every --sqfs,
// --bind-mount and --tmpfs entry is known.
util::expected<std::vector<tmpfs_tuple>, std::string>
canonical_tmpfs_list(std::vector<tmpfs_tuple> tmpfs);
util::expected<std::vector<bindmount_pair>, std::string>
canonical_bindmount_list(std::vector<bindmount_pair> bind_mounts);

// Which CLI flag a mount_order_entry's payload came from.
enum class mount_kind { sqfs, bind_mount, tmpfs };

struct mount_order_entry {
    mount_kind kind;
    // index into the mounts, bind_mounts or tmpfs vector, per `kind`
    std::size_t index;
};
using mount_order = std::vector<mount_order_entry>;

// Order --sqfs, --bind-mount and --tmpfs entries so that each is mounted
// after the mount that provides its mount point (parents before children),
// regardless of which flag was used for which or what order they were given
// on the command line. A --bind-mount or --tmpfs can therefore be placed
// inside a squashfs image, and a squashfs image inside a --tmpfs.
//
// The order is the lexical order of the destinations, which already puts
// parents first: paths compare component by component, so a path sorts
// before every path below it. Nothing else constrains the order: a
// --bind-mount source or a squashfs image is always the host's, taken before
// anything is mounted, also when it lies inside another destination (see
// capture_sources in mount_rootless.cpp).
//
// Errors: two entries with an identical destination (mirrors the duplicate
// mount-point check in validate_mount_list for --sqfs).
util::expected<mount_order, std::string>
build_mount_order(const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_tuple>& tmpfs);

// the destination of the --sqfs, --bind-mount or --tmpfs entry `e` refers to
const std::filesystem::path&
mount_destination(const mount_order_entry& e, const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_tuple>& tmpfs);

// For each entry of `order`, the kind of the requested mount that provides
// its mount point: the innermost other --sqfs, --bind-mount or --tmpfs
// destination that it lies strictly below, or nullopt if it lies on the
// host's own file system. With `order` from build_mount_order, the provider
// is always mounted before the entry it provides.
//
// The provider decides whether a missing mount point can be created: in a
// --tmpfs always (it is empty, writable and private to the mount namespace),
// on the host only with a mutable root, and never in a squashfs image (read
// only) or a bind mount (which would write into its source on the host).
std::vector<std::optional<mount_kind>> mount_point_providers(
    const mount_list& mounts, const std::vector<bindmount_pair>& bind_mounts,
    const std::vector<tmpfs_tuple>& tmpfs, const mount_order& order);

// Check the mount point of every --sqfs, --bind-mount and --tmpfs entry that
// is on the host, or inside a --bind-mount. One inside a --sqfs or --tmpfs
// destination is provided by that mount (see mount_point_providers), and can
// not be checked before it is mounted. Paths must be in canonical form
// (canonical_mount_path).
//
// Without `mutable_root`, a mount point on the host must exist. With it, a
// missing one is created in the mutable root, on the tmpfs copy of the path
// to it (see mutable_root_shadow_dirs); only a file or a dangling symlink in
// its path is an error.
//
// Every --bind-mount source must be an existing directory on the host, with
// or without `mutable_root`, also one inside another requested destination:
// a bind source is always the host's directory (see build_mount_order). So
// a mount point whose innermost provider is a --bind-mount is checked too,
// at the same path below the bind's source: it is never created, mutable
// root or not, as that would write into the source on the host.
util::expected<void, std::string>
validate_mount_points(const mount_list& mounts,
                      const std::vector<bindmount_pair>& bind_mounts,
                      const std::vector<tmpfs_tuple>& tmpfs, bool mutable_root);

// The deepest ancestor of `path` that exists (`path` itself if it exists).
// Every existing component must be a directory, so a regular file or a
// dangling symlink in the path is an error. `path` must be canonical.
util::expected<std::filesystem::path, std::string>
deepest_existing_ancestor(const std::filesystem::path& path);

// The directories a mutable root rebuilds on its tmpfs ("shadows") so that
// every missing mount point in `dsts` can be created there without writing to
// the host: "/", and each directory from "/" down to the deepest existing
// ancestor of a missing mount point. Everything else in the mutable root is a
// bind mount of the host. `dsts` must be canonical.
util::expected<std::set<std::filesystem::path>, std::string>
mutable_root_shadow_dirs(const std::set<std::filesystem::path>& dsts);

// With a mutable root: each missing mount point on the host whose deepest
// existing ancestor, other than "/", is writable by the caller, paired with
// that ancestor. The ancestor is shadowed in tmpfs, so for the command, new
// files written directly in it are lost on exit, and a file in it can not be
// replaced by renaming another over it (EBUSY: it is a bind mount), as editors
// and shell history do. Callers warn about these.
std::vector<std::pair<std::filesystem::path, std::filesystem::path>>
writable_shadowed_dirs(const mount_list& mounts,
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
