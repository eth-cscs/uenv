#pragma once

#include "uenv/mount.h"
#include <semaphore.h>

namespace uenv {

/* Variables for coordinating join */
struct join_t {
    bool winner_p;
    std::string sem_name;
    sem_t* sem;
    std::string shm_name;
    struct {
        pid_t winner_pid;
        int proc_left_ct; // serial-only access
    }* shared;
};

/* Same effect as `unshare --mount --map-root-user` */
util::expected<void, std::string> unshare_mount_map_root();

/* go back to effective user */
util::expected<void, std::string> map_effective_user(uid_t uid, gid_t gid);

/* squashfs_ll mount */
util::expected<void, std::string> do_sqfs_mount(const uenv::mount_pair&,
                                                bool fuse_st);

util::expected<void, std::string> make_mutable_root();

void join_begin(join_t& join, std::string join_tag);
void join_end(join_t& join, int join_ct);

void namespaces_join(pid_t pid);

} // namespace uenv
