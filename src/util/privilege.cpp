#include <cerrno>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/privilege.h>

namespace util {

//
// reading ids
//

util::expected<process_ids, std::string> current_ids() {
    process_ids p;
    if (getresuid(&p.real.uid, &p.effective.uid, &p.saved.uid) != 0) {
        return util::unexpected(
            fmt::format("getresuid failed: {}", strerror(errno)));
    }
    if (getresgid(&p.real.gid, &p.effective.gid, &p.saved.gid) != 0) {
        return util::unexpected(
            fmt::format("getresgid failed: {}", strerror(errno)));
    }
    const int n = getgroups(0, nullptr);
    if (n < 0) {
        return util::unexpected(
            fmt::format("getgroups failed: {}", strerror(errno)));
    }
    p.groups.resize(static_cast<std::size_t>(n));
    if (n > 0 && getgroups(n, p.groups.data()) < 0) {
        return util::unexpected(
            fmt::format("getgroups failed: {}", strerror(errno)));
    }
    return p;
}

util::expected<user_ids, std::string> current_user() {
    auto p = current_ids();
    if (!p) {
        return util::unexpected(p.error());
    }
    return user_ids{.id = p->real, .groups = std::move(p->groups)};
}

util::expected<void, std::string> verify_ids(ids expected) {
    auto p = current_ids();
    if (!p) {
        return util::unexpected(p.error());
    }
    if (p->real.uid != expected.uid || p->effective.uid != expected.uid ||
        p->saved.uid != expected.uid) {
        return util::unexpected(fmt::format("uid is {}/{}/{}, expected {}",
                                            p->real.uid, p->effective.uid,
                                            p->saved.uid, expected.uid));
    }
    if (p->real.gid != expected.gid || p->effective.gid != expected.gid ||
        p->saved.gid != expected.gid) {
        return util::unexpected(fmt::format("gid is {}/{}/{}, expected {}",
                                            p->real.gid, p->effective.gid,
                                            p->saved.gid, expected.gid));
    }
    return {};
}

//
// process-wide changes
//

// These use the glibc wrappers deliberately: they are for a single-threaded
// process whose whole identity is to change.

util::expected<void, std::string> set_effective_uid(uid_t uid) {
    if (seteuid(uid) != 0) {
        return util::unexpected(fmt::format(
            "unable to set the effective uid to {}: {}", uid, strerror(errno)));
    }
    if (geteuid() != uid) {
        return util::unexpected(fmt::format(
            "unable to set the effective uid to {}: it is {}", uid, geteuid()));
    }
    return {};
}

util::expected<void, std::string> become_root() {
    if (seteuid(0) != 0) {
        return util::unexpected(fmt::format(
            "failed to reclaim the effective uid: {}", strerror(errno)));
    }
    if (setreuid(0, 0) != 0) {
        return util::unexpected(
            fmt::format("failed to setreuid: {}", strerror(errno)));
    }
    if (geteuid() != 0 || getuid() != 0) {
        return util::unexpected("setreuid reported success but the process is "
                                "not root");
    }
    return {};
}

util::expected<void, std::string> drop_privileges(ids target) {
    // Drop the gid before the uid: once the effective uid is not root the
    // process can no longer change its gids at all. setresgid, not setegid,
    // because the saved set-group-id survives execve - leaving it at 0 would
    // let the exec'd command call setegid(0) and regain the group. Note that
    // PR_SET_NO_NEW_PRIVS does not prevent that: it blocks gaining *new*
    // privileges through execve, not regaining a saved id.
    if (setresgid(target.gid, target.gid, target.gid) != 0) {
        return util::unexpected(
            fmt::format("setresgid failed: {}", strerror(errno)));
    }

    // set real, effective, saved user id.
    if (setresuid(target.uid, target.uid, target.uid) != 0) {
        return util::unexpected(
            fmt::format("setresuid failed: {}", strerror(errno)));
    }

    // Read the ids back rather than trusting the return values: an unnoticed
    // failure here would exec the user's command with privilege.
    if (auto r = verify_ids(target); !r) {
        return util::unexpected(
            fmt::format("failed to drop privileges: {}", r.error()));
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return util::unexpected("PR_SET_NO_NEW_PRIVS failed");
    }

    // A setuid process is not dumpable, and stays that way through the id
    // changes above. Now that every id is the target's and no privilege can be
    // regained, the process is an ordinary one of the target user, and is
    // marked as such: the execve that normally follows would do the same, and
    // a process that exits instead needs it for anything that attaches to it
    // as that user, such as the leak check a sanitizer build runs at exit.
    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0) {
        return util::unexpected("PR_SET_DUMPABLE failed");
    }
    return {};
}

//
// thread-confined changes
//

namespace {

//
// raw credential syscalls
//

// These deliberately bypass the glibc wrappers.
//
// glibc implements setuid/setgid/setgroups as "setxid" operations: it signals
// every other thread in the process and has each one apply the same change, so
// that the process appears to have a single identity, as POSIX requires. The
// kernel has no such notion - credentials live in the task - so going straight
// to the syscall changes this thread only, which is precisely what
// run_as_user() promises.
//
// Do not "clean these up" into setresuid()/setgroups(): that would silently
// convert a one-thread change into a whole-process one.

int raw_setgroups(const std::vector<gid_t>& groups) {
    return static_cast<int>(
        ::syscall(SYS_setgroups, groups.size(), groups.data()));
}

int raw_setresgid(gid_t gid) {
    return static_cast<int>(::syscall(SYS_setresgid, gid, gid, gid));
}

int raw_setresuid(uid_t uid) {
    return static_cast<int>(::syscall(SYS_setresuid, uid, uid, uid));
}

// Assume `user` on the calling thread, irreversibly. Order is forced: the
// group calls need CAP_SETGID, and the uid change is what gives that up, so it
// has to come last.
util::expected<void, std::string> assume(const user_ids& user) {
    if (raw_setgroups(user.groups) != 0) {
        return util::unexpected(
            fmt::format("unable to set {} supplementary group(s): {}",
                        user.groups.size(), strerror(errno)));
    }
    if (raw_setresgid(user.id.gid) != 0) {
        return util::unexpected(fmt::format("unable to set gid {}: {}",
                                            user.id.gid, strerror(errno)));
    }
    if (raw_setresuid(user.id.uid) != 0) {
        return util::unexpected(fmt::format("unable to set uid {}: {}",
                                            user.id.uid, strerror(errno)));
    }

    // Read the ids back rather than trusting the return values: this thread is
    // about to act on an unprivileged user's behalf, and a silently incomplete
    // drop would do it with privilege.
    if (auto r = verify_ids(user.id); !r) {
        return util::unexpected(
            fmt::format("failed to assume the user's identity: {}", r.error()));
    }
    return {};
}

} // namespace

util::expected<void, std::string>
run_as_user(const user_ids& user, const std::function<void()>& work) {
    std::optional<std::string> error;

    std::thread worker{[&]() noexcept {
        // Credentials cannot be changed without privilege. Rather than run the
        // work with the wrong identity, require that the process already has
        // the one that was asked for.
        if (geteuid() != 0) {
            if (getuid() != user.id.uid || getgid() != user.id.gid) {
                error = fmt::format(
                    "unable to assume uid {} gid {}: not privileged, and the "
                    "process is uid {} gid {}",
                    user.id.uid, user.id.gid, getuid(), getgid());
                return;
            }
            spdlog::debug("run_as_user: already running as uid {}",
                          user.id.uid);
        } else if (auto r = assume(user); !r) {
            error = r.error();
            return;
        }

        // Nothing may escape into the calling process: this thread runs inside
        // slurmstepd, where an exception crossing back out would terminate a
        // root daemon.
        try {
            work();
        } catch (const std::exception& e) {
            error = fmt::format("unhandled exception: {}", e.what());
        } catch (...) {
            error = "unhandled exception";
        }
    }};
    worker.join();

    if (error) {
        return util::unexpected(error.value());
    }
    return {};
}

} // namespace util
