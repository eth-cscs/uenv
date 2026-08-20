#pragma once

#include "uenv/mount.h"

namespace uenv {
namespace rootless {

// Same effect as `unshare --mount --map-root-user`
util::expected<void, std::string> unshare_mount_map_root();

// go back to effective user
util::expected<void, std::string> map_effective_user(uid_t uid, gid_t gid);

// restore the calling process's dumpable attribute to `dumps_allowed` (its
// state before the fake-root dance forced it on -- see mount_and_join_ns),
// and disallow gaining any new privileges. Only safe to call once nothing
// else still needs to open /proc/<this pid>/ns/* for this process (e.g.
// after every peer sharing this mount has joined it).
util::expected<void, std::string> lock_down(bool dumps_allowed);

// post hook for the rootless squashfs-mount build: exit the fake-root user
// namespace and disallow gaining any new privileges. `dumps_allowed` is the
// caller's dumpable state before unshare_and_mount forced it on.
util::expected<void, std::string> exit_sandbox(uid_t uid, gid_t gid,
                                               bool dumps_allowed);

// mount using squashfuse low-level interface
util::expected<void, std::string> do_sqfs_ll_mount(const uenv::mount_pair&,
                                                   bool fuse_st);

// unshare into a new mount namespace as fake root, then mount each image in
// `mounts` at its target mount point.
util::expected<void, std::string>
unshare_and_mount(const uenv::mount_list& mounts, bool fuse_single_threaded);

// Coordinate SquashFS mounting across `ntasks` peer processes that share a
// node: elect a leader (via a proc_barrier tagged `tag`), which performs the
// mounts and drops privileges, while every other task joins the leader's
// user/mount namespaces. When ntasks == 1 the barrier is skipped entirely
// and this process just mounts and drops privileges by itself.
//
// The calling process's dumpable state is captured up front and restored
// once it is safe to do so, so a caller that started out non-dumpable ends
// up non-dumpable again, and one that started out dumpable stays dumpable --
// callers do not need their own policy for this.
util::expected<void, std::string>
mount_and_join_ns(const std::string& tag, int ntasks,
                  const uenv::mount_list& mounts, bool fuse_single_threaded,
                  uid_t uid, gid_t gid);

} // namespace rootless
} // namespace uenv
