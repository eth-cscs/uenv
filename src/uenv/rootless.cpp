#include <csignal>
#include <cstdlib>
#include <cstring>
#include <err.h>
#include <fmt/format.h>
#include <sched.h>
extern "C" {
#include <squashfuse/ll.h>
}
#include <util/macros.h>
#include "posix_io.h"
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <stddef.h>
#include <string>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <uenv/mount.h>
#include <unistd.h>
#include <util/expected.h>
#include <util/ready_fork.h>

namespace uenv {
namespace rootless {

// Same effect as `unshare --mount --map-root-user`
util::expected<void, std::string> unshare_mount_map_root() {
    spdlog::trace("become fake root");
    int uid = getuid(); // get current uid
    int gid = getgid();

    Z_e(unshare(CLONE_NEWUSER | CLONE_NEWNS));
    // enable coredumps, otherwise we cannot write to uid_map/gid_map etc.
    Z_e(prctl(PR_SET_DUMPABLE, 1));

    if (auto r =
            mount(std::nullopt, "/", std::nullopt, MS_SHARED | MS_REC, nullptr);
        !r) {
        return r;
    }

    // map current uid to root
    char buf[256];
    auto proc_uid_map = openat(AT_FDCWD, "/proc/self/uid_map", O_WRONLY);
    if (!proc_uid_map)
        return util::unexpected(proc_uid_map.error());
    snprintf(buf, sizeof(buf), "0 %d 1", uid);
    if (auto r = write(proc_uid_map.value(), buf, strlen(buf)); !r) {
        return r;
    }
    Z_e(close(proc_uid_map.value()));

    // write /proc/self/gid_setgroups -> deny
    auto proc_setgroups = openat(AT_FDCWD, "/proc/self/setgroups", O_WRONLY);
    if (!proc_setgroups)
        return util::unexpected(proc_setgroups.error());
    if (auto r = write(proc_setgroups.value(), "deny", 4); !r) {
        return r;
    }
    Z_e(close(proc_setgroups.value()));

    // map gid  to root group
    auto proc_gid_map = openat(AT_FDCWD, "/proc/self/gid_map", O_WRONLY);
    if (!proc_gid_map)
        return util::unexpected(proc_gid_map.error());
    snprintf(buf, sizeof(buf), "0 %d 1", gid);
    if (auto r = write(proc_gid_map.value(), buf, strlen(buf)); !r) {
        return r;
    }
    Z_e(close(proc_gid_map.value()));

    // the following is executed by `unshare --mount --map-root-user`
    if (auto r = mount("none", "/", std::nullopt, MS_REC | MS_PRIVATE, nullptr);
        !r) {
        return r;
    }
    return {};
}

// go back to effective user
util::expected<void, std::string> map_effective_user(uid_t uid, gid_t gid) {

    Z_e(unshare(CLONE_NEWUSER | CLONE_NEWNS));
    // map current user id to root
    char buf[256];
    spdlog::trace("map_effective_user({}, {})", uid, gid);
    auto proc_uid_map = openat(AT_FDCWD, "/proc/self/uid_map", O_WRONLY);
    if (!proc_uid_map)
        return util::unexpected(proc_uid_map.error());
    sprintf(buf, "%d 0 1", uid);
    if (auto r = write(proc_uid_map.value(), buf, strlen(buf)); !r) {
        return r;
    }
    Z_e(close(proc_uid_map.value()));

    // note: setgroups is already "deny" here, inherited from the enclosing
    // fake-root namespace (unshare_mount_map_root) -- once a namespace's
    // setgroups is "deny", all descendant namespaces are permanently locked
    // to "deny" too, so writing "allow" here would always fail with EPERM.

    auto proc_gid_map = openat(AT_FDCWD, "/proc/self/gid_map", O_WRONLY);
    if (!proc_gid_map)
        return util::unexpected(proc_gid_map.error());
    sprintf(buf, "%d 0 1", gid);
    if (auto r = write(proc_gid_map.value(), buf, strlen(buf)); !r) {
        return r;
    }
    Z_e(close(proc_gid_map.value()));

    // disable coredump again (slurm policy)
    // this breaks both slurm plugin and squashfs-mount, as other tasks can't
    // access the /proc/pid/ns anymore
    // Z_e(prctl(PR_SET_DUMPABLE, 0));
    return {};
}

util::expected<void, std::string> exit_sandbox(uid_t uid, gid_t gid) {
    if (auto r = map_effective_user(uid, gid); !r) {
        return r;
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return util::unexpected("PR_SET_NO_NEW_PRIVS failed");
    }
    return {};
}

// The forked child shares the parent's entire call stack. A plain `return`
// from an error path here would unwind back into the caller's normal control
// flow *inside the child*, which would then go on to do whatever the parent
// is supposed to do next (e.g. mount further images, exec the target
// command) -- running it twice, once in each process. Error paths in the
// child must therefore terminate the child directly instead of returning.
[[noreturn]] static void child_fail(const std::string& msg) {
    spdlog::error("{}", msg);
    _exit(1);
}

static void init_fs_ops(struct fuse_lowlevel_ops* sqfs_ll_ops) {
    memset(sqfs_ll_ops, 0, sizeof(*sqfs_ll_ops));
    sqfs_ll_ops->getattr = sqfs_ll_op_getattr;
    sqfs_ll_ops->opendir = sqfs_ll_op_opendir;
    sqfs_ll_ops->releasedir = sqfs_ll_op_releasedir;
    sqfs_ll_ops->readdir = sqfs_ll_op_readdir;
    sqfs_ll_ops->lookup = sqfs_ll_op_lookup;
    sqfs_ll_ops->open = sqfs_ll_op_open;
    sqfs_ll_ops->create = sqfs_ll_op_create;
    sqfs_ll_ops->release = sqfs_ll_op_release;
    sqfs_ll_ops->read = sqfs_ll_op_read;
    sqfs_ll_ops->readlink = sqfs_ll_op_readlink;
    sqfs_ll_ops->listxattr = sqfs_ll_op_listxattr;
    sqfs_ll_ops->getxattr = sqfs_ll_op_getxattr;
    sqfs_ll_ops->forget = sqfs_ll_op_forget;
    sqfs_ll_ops->statfs = stfs_ll_op_statfs;
}

// Mount a squashfs image in the background (forked process), unmount the image
// when the parent process exits (shell is closed).
// Adapted from `squashfuse_ll` written by Dave Vasilevsky <dave@vasilevsky.ca>
// Ref: https://github.com/vasi/squashfuse/blob/master/ll_main.c
util::expected<void, std::string> do_sqfs_ll_mount(const mount_pair& entry,
                                                   bool fuse_st) {
    spdlog::trace("do_sqfs_ll_mount");
    // use a pipe to synchronize parent and child process
    auto rf = util::ready_fork::create();
    if (!rf) {
        return util::unexpected(rf.error());
    }

    pid_t pid = rf->fork();
    if (pid < 0) {
        return util::unexpected(
            fmt::format("fork() failed: {}", strerror(errno)));
    }

    if (pid == 0) {

        // kill the fuse process when the parent exits
        prctl(PR_SET_PDEATHSIG, SIGHUP);

        // do not listen for SIGINT (ctrl+c)
        signal(SIGINT, SIG_IGN);

        // setup dummy fuse_args to make sqfs_ll_open happy
        struct fuse_args args;
        std::string dummy{"dummy"};
        std::vector<char*> dummy_args{const_cast<char*>(dummy.c_str()),
                                      nullptr};
        args.argc = 1;
        args.argv = dummy_args.data();
        args.allocated = 0;
        int err;
        sqfs_ll* ll;

        // define squashfs file system operations
        struct fuse_lowlevel_ops sqfs_ll_ops;
        init_fs_ops(&sqfs_ll_ops);

        // fuse_daemonize() will unconditionally clobber fds 0-2.
        // If we get one of these file descriptors in sqfs_ll_open,
        // we're going to have a bad time. Just make sure that all
        // these fds are open before opening the image file, that way
        // we must get a different fd.
        while (true) {
            int fd = open("/dev/null", O_RDONLY);
            if (fd == -1) {
                // Can't open /dev/null, how bizarre! However,
                // fuse_deamonize won't clobber fds if it can't
                // open /dev/null either, so we ought to be OK.
                break;
            }
            if (fd > 2) {
                // fds 0-2 are now guaranteed to be open.
                close(fd);
                break;
            }
        }

        int idle_timeout_secs = 0;
        // the child process starts fuse and informs the calling process via
        // pipe when done

        int offset = 0;
        // OPEN FS
        err = !(ll = sqfs_ll_open(entry.sqfs.c_str(), offset));
        if (err) {
            child_fail("sqfs_ll_open failed");
        }
        if (!err) {
            // startup fuse
            sqfs_ll_chan ch;
            // err = -1;
            sqfs_err sqfs_ret =
                sqfs_ll_mount(&ch, entry.mount.c_str(), &args, &sqfs_ll_ops,
                              sizeof(sqfs_ll_ops), ll);
            if (sqfs_ret == SQFS_OK) {

                if (sqfs_ll_daemonize(true /*foreground*/) != -1) {
                    // inform parent process that sqfs has been mounted
                    rf->notify_ready();

                    // setup signal handlers and enter fuse_session_loop
                    if (fuse_set_signal_handlers(ch.session) != -1) {
                        if (idle_timeout_secs) {
                            setup_idle_timeout(ch.session, idle_timeout_secs);
                        }
#ifdef SQFS_MULTITHREADED
#if FUSE_USE_VERSION >= 30
                        if (!fuse_st) {
                            struct fuse_loop_config config;
                            config.clone_fd = 1;
                            config.max_idle_threads = 10;
                            err = fuse_session_loop_mt(ch.session, &config);
                        }
#else  /* FUSE_USE_VERSION < 30 */
                        if (!fuse_st) {
                            err = fuse_session_loop_mt(ch.session);
                        }
#endif /* FUSE_USE_VERSION */
                        else
#endif
                            err = fuse_session_loop(ch.session);

                        teardown_idle_timeout();
                        fuse_remove_signal_handlers(ch.session);
                    } else {
                        child_fail("set signal handlers failed.");
                    }
                } else {
                    child_fail("daemonize failed");
                }
                sqfs_ll_destroy(ll);

                // Rely on OS cleanup for the actual unmount, to avoid
                // additional synchronization when mounts are nested:
                // `sqfs_ll_unmount` would have to be executed in reverse
                // order. When this line is reached, the computational tasks
                // have already finished.
                // `sqfs_ll_unmount` doesn't fail, but it calls
                // `fuse_session_unmount`, which prints an error message
                // `fuse: failed to unmount ...: No such file or directory`
                //
                // sqfs_ll_unmount(&ch, entry.mount.c_str());
                //
                // Still free the `fuse_session` allocated by
                // `sqfs_ll_mount`/`fuse_session_new`, otherwise it leaks.
                fuse_session_destroy(ch.session);
            } else {
                switch (sqfs_ret) {
                case SQFS_ERR: {
                    child_fail(fmt::format("SQFS_ERR {} ", strerror(errno)));
                }
                case SQFS_BADFORMAT: {
                    child_fail("SQFS_BADFORMAT (unsupported file format)");
                }
                case SQFS_BADVERSION: {
                    child_fail("SQFS_BADVERSION");
                }
                case SQFS_BADCOMP: {
                    child_fail("SQFS_BADCOMP");
                }
                case SQFS_UNSUP: {
                    child_fail("SQFS_UNSUP, unsupported feature");
                }
                case SQFS_OK: {
                    break;
                }
                }
            }
        } else {
            child_fail("sqfs_ll_open_failed");
        }
        fuse_opt_free_args(&args);
        free(ll);
        exit(0);
    }

    // parent block on pipe until fusemount has finished.
    if (auto ok = rf->wait_ready(); !ok) {
        return util::unexpected(fmt::format(
            "mounting squashfs image {} at {} failed: {}", entry.sqfs.string(),
            entry.mount.string(), ok.error()));
    }

    return {};
}
} // namespace rootless
} // namespace uenv
