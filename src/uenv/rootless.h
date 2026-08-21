#pragma once

#include "uenv/mount.h"

namespace uenv {
namespace rootless {

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
