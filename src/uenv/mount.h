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

//
// mounts as written on the command line or in UENV_MOUNT_LIST
//
// These are the output of the parsers in parse.h. Paths are strings, exactly
// as they were given: nothing has been resolved or checked.
//

struct mount_description {
    std::string sqfs_path;
    std::string mount_path;
};

struct bindmount_description {
    std::string src;
    std::string dst;
};

struct tmpfs_description {
    std::string mount;
    std::optional<std::uint64_t> size;
};

// every mount that was requested: the values of --sqfs (or UENV_MOUNT_LIST),
// --bind-mount and --tmpfs
struct mount_request {
    std::vector<mount_description> sqfs;
    std::vector<bindmount_description> bind_mounts;
    std::vector<tmpfs_description> tmpfs;
};

//
// mounts that are ready to be mounted
//
// Every path is in canonical form: absolute (a relative path is resolved
// against the working directory), lexically normal, with symlinks in the part
// that exists resolved, and without a trailing separator. That is the one
// spelling that paths are compared in by the functions below.
//

struct mount_pair {
    std::filesystem::path sqfs;
    std::filesystem::path mount;
};

struct tmpfs_mount {
    std::filesystem::path mount;
    std::optional<std::uint64_t> size;
};

struct bindmount_pair {
    std::filesystem::path src;
    std::filesystem::path dst;
};

using mount_list = std::vector<mount_pair>;

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
// Errors: two entries with an identical destination.
util::expected<mount_order, std::string>
build_mount_order(const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_mount>& tmpfs);

// the destination of the --sqfs, --bind-mount or --tmpfs entry `e` refers to
const std::filesystem::path&
mount_destination(const mount_order_entry& e, const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_mount>& tmpfs);

// For each entry of `order`, the kind of the requested mount that provides
// its mount point: the innermost other --sqfs, --bind-mount or --tmpfs
// destination that it lies strictly below, or nullopt if it lies on the
// host's own file system. With `order` from build_mount_order, the provider
// is always mounted before the entry it provides.
//
// The provider decides whether a missing mount point can be created: in a
// --tmpfs always (it is empty, writable and private to the mount namespace),
// and never on the host, in a squashfs image (read only) or in a bind mount
// (which would write into its source on the host).
std::vector<std::optional<mount_kind>> mount_point_providers(
    const mount_list& mounts, const std::vector<bindmount_pair>& bind_mounts,
    const std::vector<tmpfs_mount>& tmpfs, const mount_order& order);

// Check the mount point of every --sqfs, --bind-mount and --tmpfs entry that
// is on the host, or inside a --bind-mount. One inside a --sqfs or --tmpfs
// destination is provided by that mount (see mount_point_providers), and can
// not be checked before it is mounted. Paths must be in canonical form
// (canonical_mount_path).
//
// Errors, in this order: two entries with an identical destination (the same
// error as build_mount_order), then the first mount point or source that
// fails one of the checks below.
//
// A mount point on the host must exist.
//
// Every --bind-mount source must be an existing directory on the host, also
// one inside another requested destination: a bind source is always the
// host's directory (see build_mount_order). So a mount point whose innermost
// provider is a --bind-mount is checked too, at the same path below the
// bind's source: it is never created, as that would write into the source on
// the host.
util::expected<void, std::string>
validate_mount_points(const mount_list& mounts,
                      const std::vector<bindmount_pair>& bind_mounts,
                      const std::vector<tmpfs_mount>& tmpfs);

// Everything that is to be mounted, validated and in the order to mount it.
struct mount_plan {
    // sorted by mount point, so that an image is before the images that are
    // mounted inside it
    mount_list mounts;
    std::vector<bindmount_pair> bind_mounts;
    std::vector<tmpfs_mount> tmpfs;
    // the order to mount the entries of the three lists above in
    // (build_mount_order)
    mount_order order;
};

// Turn the mounts that were requested into the mounts to perform:
// - every path is put in canonical form;
// - every squashfs image is checked: it is a regular file that the caller can
//   read, and it starts with the squashfs magic number;
// - no two entries have the same destination, and every mount point and
//   bind-mount source that can be checked before mounting is an existing
//   directory (validate_mount_points);
// - the entries are ordered so that each is mounted after the mount that
//   provides its mount point (build_mount_order).
//
// The same function serves both mount backends. The kernel backend has no
// --bind-mount or --tmpfs, so its requests only have sqfs entries.
//
// This examines paths that the caller named, so it must run with the
// credentials of the calling user. Both privileged callers of the kernel
// backend (the setuid helper and the Slurm plugin) do so: do not move it above
// a privilege elevation, where it would report on paths that the user can not
// see.
util::expected<mount_plan, std::string>
make_mount_plan(const mount_request& request);

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
