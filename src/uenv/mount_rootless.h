#pragma once

#include <filesystem>
#include <set>

#include "uenv/mount.h"

namespace uenv {
namespace rootless {

// Coordinate SquashFS mounting across `ntasks` peer processes that share a
// node: elect a leader (via a proc_barrier tagged `tag`), which performs the
// mounts and drops privileges, while every other task joins the leader's
// user/mount namespaces. When ntasks == 1 the barrier is skipped entirely
// and this process just mounts and drops privileges by itself.
//
// The mount's lifetime is owned by:
//
//   ntasks == 1  the process that execs the command, via PR_SET_PDEATHSIG.
//                There is one user, and this path must also work outside
//                Slurm (a login node), where nothing else would clean up.
//   ntasks > 1   a supervisor process forked by the leader, reaped by
//                slurmstepd's end-of-step sweep of the step's cgroup. The
//                leader is an arbitrary rank, so anchoring the mount to its
//                command would take the uenv away from every other rank as
//                soon as that one finished.
//
// The calling process's dumpable state is captured up front and restored
// once it is safe to do so, so a caller that started out non-dumpable ends
// up non-dumpable again, and one that started out dumpable stays dumpable --
// callers do not need their own policy for this.
// `order` gives the application order across `mounts`, `bind_mounts` and
// `tmpfs` (see uenv::build_mount_order()): a parent is always mounted before
// anything nested under it, and a bind mount after the mount providing its
// source.
util::expected<void, std::string> mount_and_join_ns(
    const std::string& tag, int ntasks, const uenv::mount_list& mounts,
    const std::vector<uenv::bindmount_pair>& bind_mounts,
    const std::vector<uenv::tmpfs_tuple>& tmpfs, const uenv::mount_order& order,
    bool fuse_single_threaded, uid_t uid, gid_t gid, bool mutable_root);

// Rebuild "/" on a tmpfs (inspired by bubblewrap), so that mount points which
// do not exist on the host can be created before mounting onto them. Each
// directory in `shadow` (see uenv::mutable_root_shadow_dirs(); it must
// contain "/") is recreated on the tmpfs, holding a bind mount of each of the
// host's entries, a copy of each symlink, and the shadowed directories below
// it. Everything else is a bind mount of the host, so writes there reach the
// host as usual. Must be called after unshare_mount_map_root(), inside the
// mount namespace that owns the mounts being set up.
//
// Errors: a shadowed directory on an autofs mount (its entries are mounted
// on demand, and a copy would only hold the ones mounted now), or with more
// than max_shadow_entries entries.
//
// Only implemented for the rootless/fuse backend -- the setuid kernel
// backend does not compile this file in.
inline constexpr std::size_t max_shadow_entries = 4096;
util::expected<void, std::string>
make_mutable_root(const std::set<std::filesystem::path>& shadow);

} // namespace rootless
} // namespace uenv
