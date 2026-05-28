#pragma once

#include <filesystem>
#include <optional>
#include <string>
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
    // pre-opened read-only fd for the squashfs file, set before privilege
    // escalation so NFS root_squash does not remap euid=0 to nobody.
    // do_mount uses /proc/self/fd/N as the source when this is set.
    std::optional<int> sqfs_fd = std::nullopt;
};

// convert a description to a mount_pair that has a validated squashfs path
util::expected<mount_pair, std::string>
make_mount_pair(const mount_description& description);

using mount_list = std::vector<mount_pair>;

// one shot from string description to sorted and validated inputs
//
// auto mountvar ;
// if (auto mountvar = env.get("UENV_MOUNT_LIST")) {
//     auto mounts = parse_and_validate_mounts(mountvar.value());
// }
util::expected<mount_list, std::string>
parse_and_validate_mounts(const std::string& description);

/// close any open sqfs_fd entries in the mount list and reset them to nullopt
void close_sqfs_fds(mount_list& mounts) noexcept;

/// called as root, in slurm-plugin
util::expected<void, std::string> unshare_as_root();

/// mount sqfs images, make sure mnt ns has been unshared before calling this
/// function
util::expected<void, std::string> do_mount(const mount_list& mount_entries);

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
