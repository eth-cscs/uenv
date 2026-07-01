#include <csignal>
#include <cstdlib>
#include <cstring>
#include <err.h>
#include <fmt/format.h>
#include <sched.h>
extern "C" {
#include <squashfuse/ll.h>
}
#include "macros.h"
#include "rootless.h"
#include <semaphore.h>
#include <spdlog/spdlog.h>
#include <stddef.h>
#include <string>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <uenv/mount.h>
#include <unistd.h>
#include <util/expected.h>

// Timeout in seconds for waiting for join semaphore.
#define JOIN_TIMEOUT 30

namespace uenv {

// Join a specific namespace.
void namespace_join(pid_t pid, const char* ns) {
    std::string path;
    int fd;

    path = fmt::format("/proc/{}/ns/{}", pid, ns);
    fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT)
            Tf_(0, "join: no PID {}: {} not found", pid, path);
        else
            Tfe(0, "join: can't open {}", path);
    }
    // setns(2) seems to be involved in some kind of race with syslog(3).
    // Rarely, when configured with --enable-syslog, the call fails with
    // EINVAL. We never figured out a proper fix, so just retry a few times in
    // a loop. See issue #1270.
    for (int i = 1; setns(fd, 0) != 0; i++)
        if (i >= 5) {
            spdlog::error("can’t join {} namespace of pid {}", ns, pid);
            exit(1);
        } else {
            spdlog::warn("can’t join {} namespace; trying again", ns);
            sleep(1);
        }
}

// Join the existing namespaces containing process pid, which could be the
// join winner or another process.
void namespaces_join(pid_t pid) {
    spdlog::warn("joining namespaces of pid {}", pid);
    namespace_join(pid, "user");
    namespace_join(pid, "mnt");
}

// Wait for semaphore sem for up to timeout seconds. If timeout or an error,
// exit unsuccessfully.
void sem_timedwait_relative(sem_t* sem, int timeout) {
    struct timespec deadline;

    // sem_timedwait() requires a deadline rather than a timeout.
    Z_e(clock_gettime(CLOCK_REALTIME, &deadline));
    deadline.tv_sec += timeout;
    Zfe(sem_timedwait(sem, &deadline), "failure waiting for join lock");
}

// Begin coordinated section of namespace joining.
void join_begin(join_t& join, std::string join_tag) {
    int fd;
    join.sem_name = fmt::format("/uenv-run_sem-{}", join_tag);
    join.shm_name = fmt::format("/uenv-run_shm-{}", join_tag);

    // Serialize.
    join.sem = sem_open(join.sem_name.c_str(), O_CREAT, 0600, 1);
    T_e(join.sem != SEM_FAILED);
    sem_timedwait_relative(join.sem, JOIN_TIMEOUT);

    // Am I the winner?
    fd = shm_open(join.shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd > 0) {
        spdlog::trace("join:: I won! PID {}", getpid());
        join.winner_p = true;
        Z_e(ftruncate(fd, sizeof(*join.shared)));
    } else {
        std::string err{strerror(errno)};
        T_e(errno == EEXIST);
        spdlog::trace("join: I lost {}", err);
        join.winner_p = false;
        fd = shm_open(join.shm_name.c_str(), O_RDWR, 0);
        T_e(fd > 0);
    }

    join.shared = static_cast<decltype(join.shared)>(mmap(
        NULL, sizeof(*join.shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    T__(join.shared != NULL);
    Z_e(close(fd));

    // Winner keeps lock; losers parallelize (winner will be done by now).
    if (!join.winner_p)
        Z_e(sem_post(join.sem));
}

// End coordinated section of namespace joining.
void join_end(join_t& join, int join_ct, std::optional<pid_t> winner_pid) {
    if (join.winner_p) { // winner still serial
        spdlog::trace("join: winner initializing shared data");
        if (!winner_pid)
            join.shared->winner_pid = getpid();
        else
            join.shared->winner_pid = winner_pid.value();
        join.shared->proc_left_ct = join_ct;
    } else // losers serialize
        sem_timedwait_relative(join.sem, JOIN_TIMEOUT);

    join.shared->proc_left_ct--;
    spdlog::trace("join: {} peers left excluding myself",
                  join.shared->proc_left_ct);

    if (join.shared->proc_left_ct <= 0) {
        spdlog::trace("join: cleaning up IPC resources");
        Tf_(join.shared->proc_left_ct == 0,
            "join: expected 0 peers left but found {}",
            join.shared->proc_left_ct);
        Zfe(sem_unlink(join.sem_name.c_str()), "join: can't unlink sem: {}",
            join.sem_name.c_str());
        Zfe(shm_unlink(join.shm_name.c_str()), "join: can't unlink shm: {}",
            join.shm_name.c_str());
    }

    Z_e(sem_post(join.sem)); // parallelize (all)

    Z_e(munmap(join.shared, sizeof(*join.shared)));
    Z_e(sem_close(join.sem));

    spdlog::trace("join: done");
}

// Same effect as `unshare --mount --map-root-user`
util::expected<void, std::string> unshare_mount_map_root() {
    spdlog::trace("become fake root");
    int uid = getuid(); // get current uid
    int gid = getgid();
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0)
        err(EXIT_FAILURE, "unshare(CLONE_NEWUSER | CLONE_NEWNS) failed");

    if (auto r =
            mount(std::nullopt, "/", std::nullopt, MS_SHARED | MS_REC, nullptr);
        !r) {
        return r;
    }

    // map current user id to root
    char buf[256];
    int proc_uid_map = openat(AT_FDCWD, "/proc/self/uid_map", O_WRONLY);
    sprintf(buf, "0 %d 1", uid);
    write(proc_uid_map, buf, strlen(buf));
    close(proc_uid_map);

    int proc_setgroups = openat(AT_FDCWD, "/proc/self/setgroups", O_WRONLY);
    write(proc_setgroups, "deny", 4);
    close(proc_setgroups);

    int proc_gid_map = openat(AT_FDCWD, "/proc/self/gid_map", O_WRONLY);
    sprintf(buf, "0 %d 1", gid);
    write(proc_gid_map, buf, strlen(buf));
    close(proc_gid_map);

    // the following is executed by `unshare --mount --map-root-user`
    if (auto r = mount("none", "/", std::nullopt, MS_REC | MS_PRIVATE, nullptr);
        !r) {
        return r;
    }
    return {};
}

// go back to effective user
util::expected<void, std::string> map_effective_user(uid_t uid, gid_t gid) {

    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
        return util::unexpected{fmt::format(
            "unshare(CLONE_NEWUSER|CLONE_NEWNS) failed, {}", strerror(errno))};
    }
    // map current user id to root
    char buf[256];
    int proc_uid_map = openat(AT_FDCWD, "/proc/self/uid_map", O_WRONLY);
    sprintf(buf, "%d 0 1", uid);
    write(proc_uid_map, buf, strlen(buf));
    close(proc_uid_map);

    int proc_setgroups = openat(AT_FDCWD, "/proc/self/setgroups", O_WRONLY);
    write(proc_setgroups, "allow", 5);
    close(proc_setgroups);

    int proc_gid_map = openat(AT_FDCWD, "/proc/self/gid_map", O_WRONLY);
    sprintf(buf, "%d 0 1", gid);
    write(proc_gid_map, buf, strlen(buf));
    close(proc_gid_map);
    return {};
}

static util::expected<void, std::string> write_pipe(int pipe) {
    char c[32];
    // AppImage seems doing something more advanced:
    // https://github.com/AppImage/AppImageKit/blob/master/src/runtime.c#L138
    memset(c, 'x', sizeof(c));
    int res = write(pipe, c, sizeof(c));
    if (res < 0) {
        return util::unexpected{"writing to pipe failed"};
    }
    return {};
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

    spdlog::trace("do_sqfs_mount");
    // use a pipe to synchronize parent and child process
    int pipe_wait[2];
    if (pipe(pipe_wait) != 0) {
        return util::unexpected{"pipe error"};
    }

    int pid = fork();
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
            return util::unexpected{"sqfs_ll_open failed\n"};
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
                    close(pipe_wait[0]);
                    write_pipe(pipe_wait[1]);

                    // setup signlal handlers and enter fuse_session_loop
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
                        util::unexpected{"set signal handlers failed."};
                    }
                } else {
                    util::unexpected{"daemonize failed"};
                }
                sqfs_ll_destroy(ll);
                sqfs_ll_unmount(&ch, entry.mount.c_str());
            } else {
                switch (sqfs_ret) {
                case SQFS_ERR: {
                    return util::unexpected{
                        fmt::format("SQFS_ERR {} ", strerror(errno))};
                }
                case SQFS_BADFORMAT: {
                    return util::unexpected{
                        "SQFS_BADFORMAT (unsupported file format)"};
                }
                case SQFS_BADVERSION: {
                    return util::unexpected{"SQFS_BADVERSION\n"};
                }
                case SQFS_BADCOMP: {
                    return util::unexpected{"SQFS_BADCOMP\n"};
                }
                case SQFS_UNSUP: {
                    return util::unexpected{
                        "SQFS_UNSUP, unsupported feature\n"};
                }
                case SQFS_OK: {
                    break;
                }
                }
            }
        } else {
            return util::unexpected{"sqfs_ll_open_failed"};
        }
        fuse_opt_free_args(&args);
        free(ll);
        exit(0);
    } else {
        // parent block on pipe until fusemount has finished.
        char buf[256];
        close(pipe_wait[1]);
        int res = read(pipe_wait[0], buf, 256);
        if (res == 0) {
            // The child process has exited before reaching fuse_session_loop.
            util::unexpected{"mounting sqfs file failed\n"};
        }
        if (res < 0) {
            util::unexpected{"mounting sqfs file failed\n"};
        }
    }

    return {};
}

} // namespace uenv
