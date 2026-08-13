#pragma once

#include "uenv/mount.h"

namespace uenv {
namespace rootless {

// Same effect as `unshare --mount --map-root-user`
util::expected<void, std::string> unshare_mount_map_root();

// go back to effective user
util::expected<void, std::string> map_effective_user(uid_t uid, gid_t gid);

// post hook for the rootless squashfs-mount build: exit the fake-root user
// namespace and disallow gaining any new privileges.
util::expected<void, std::string> exit_sandbox(uid_t uid, gid_t gid);

// mount using squashfuse low-level interface
util::expected<void, std::string> do_sqfs_ll_mount(const uenv::mount_pair&,
                                                   bool fuse_st);

} // namespace rootless
} // namespace uenv
