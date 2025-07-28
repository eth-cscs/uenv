#pragma once

#include "uenv/mount.h"

namespace uenv {

/* Same effect as `unshare --mount --map-root-user` */
util::expected<void, std::string> unshare_mount_map_root();

/* go back to effective user */
util::expected<void, std::string> map_effective_user(uid_t uid, gid_t gid);

/* squashfs_ll mount */
util::expected<void, std::string> do_sqfs_mount(const uenv::mount_pair&);

util::expected<void, std::string> make_mutable_root();


} // namespace uenv
