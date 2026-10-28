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

// --sqfs mounts are always applied last, strictly after every --bind-mount
// and --tmpfs mount (see prepare_mount_points()/unshare_and_mount() in
// mount_rootless.cpp), regardless of what order they were given on the
// command line -- --join needs every sqfs mount to happen through a single
// rendezvous-aware step, so that ordering cannot be changed. A --bind-mount
// or --tmpfs destination nested under (or equal to) a --sqfs mount point can
// therefore never be made visible: the later --sqfs mount would silently
// bury it. Detect that unsatisfiable configuration up front and fail
// clearly, rather than silently reordering (which is not possible here) or
// warning.
//
// The opposite nesting -- an --sqfs mount point nested under a --bind-mount/
// --tmpfs destination, e.g. --tmpfs=/data --sqfs=img:/data/foo -- is fine:
// the destination mounts first, and --sqfs already applies afterwards,
// landing correctly inside it. That case is not an error here.
util::expected<void, std::string>
validate_sqfs_not_shadowed(const mount_list& mounts,
                           const std::vector<bindmount_pair>& bind_mounts,
                           const std::vector<tmpfs_tuple>& tmpfs);

// Which CLI flag a mount_order_entry's payload came from. --sqfs is
// deliberately not represented here: it is always mounted after every
// --bind-mount/--tmpfs mount (see validate_sqfs_not_shadowed above), so its
// relative order with the other two is fixed rather than participating in
// `mount_order`.
enum class mount_kind { bind_mount, tmpfs };

struct mount_order_entry {
    mount_kind kind;
    std::size_t index; // index into the bind_mounts or tmpfs vector, per `kind`
};
using mount_order = std::vector<mount_order_entry>;

// Order --bind-mount/--tmpfs entries by a lexical, stable sort of their
// destination paths: a parent directory's path is always a string prefix
// of (and sorts before) anything nested under it, so sorting this way mounts
// parents before children, regardless of which flag was used for which or
// what order they were given on the command line. Two entries with an
// identical destination are rejected as an error (mirrors the duplicate
// mount-point check in validate_mount_list for --sqfs) rather than picked
// between.
util::expected<mount_order, std::string>
build_mount_order(const std::vector<bindmount_pair>& bind_mounts,
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
