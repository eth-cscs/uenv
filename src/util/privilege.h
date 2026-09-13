#pragma once

#include <functional>
#include <string>
#include <vector>

#include <sys/types.h>

#include <util/expected.h>

// Process identity: reading the uids and gids a thread carries, changing them,
// and verifying every change by reading it back from the kernel.
//
// Every change is read back from the kernel and verified rather than trusting
// the syscall's return value, so that a partial change is reported as an error
// instead of leaving the caller with an identity it did not ask for.

namespace util {

/// A uid and gid pair: one of the three identities (real, effective, saved)
/// that a thread carries, or the identity a user is to be given.
struct ids {
    uid_t uid{};
    gid_t gid{};

    friend bool operator==(const ids&, const ids&) = default;
};

/// A user's identity for access-control purposes: uid, primary gid and the
/// supplementary groups. All three take part in a DAC decision.
struct user_ids {
    ids id;
    std::vector<gid_t> groups;

    friend bool operator==(const user_ids&, const user_ids&) = default;
};

/// The full identity of the calling thread: the real, effective and saved ids,
/// plus the supplementary groups.
struct process_ids {
    ids real;
    ids effective;
    ids saved;
    std::vector<gid_t> groups;

    friend bool operator==(const process_ids&, const process_ids&) = default;
};

/// Read the calling thread's ids with getresuid, getresgid and getgroups.
/// These are plain reads of the calling task: none of them triggers glibc's
/// setxid broadcast, so they are safe to call from a thread that has assumed
/// its own identity.
util::expected<process_ids, std::string> current_ids();

/// The calling thread's real identity: real uid, real gid and the supplementary
/// groups.
util::expected<user_ids, std::string> current_user();

/// Check that the real, effective and saved ids are all `expected`, reading
/// them back from the kernel. Called after every id change in place of trusting
/// the syscall's return value.
util::expected<void, std::string> verify_ids(ids expected);

/// Set the effective uid to `uid` and verify the result.
///
/// For a setuid binary at startup: everything that follows runs with the
/// caller's authority, while root stays in the saved set-user-id for
/// become_root() to reclaim.
util::expected<void, std::string> set_effective_uid(uid_t uid);

/// Reclaim root from the saved set-user-id, so that the real and effective uid
/// are both 0, and verify the result.
///
/// Two steps, and the order is forced. An unprivileged process may set its
/// effective uid to the saved set-user-id, but it may *not* set its real uid to
/// it (setreuid(2): "Unprivileged users may only set the real user ID to the
/// real user ID or the effective user ID"), so seteuid(0) has to come first and
/// make the process privileged before setreuid(0, 0) can move the real uid as
/// well.
util::expected<void, std::string> become_root();

/// Irreversibly become `target`, then disallow gaining any new privileges.
///
/// The real, effective and saved gid are set before the uid, because once the
/// effective uid is not root the process can no longer change its gids at all.
/// All three of each are set, so that nothing the process execs afterwards can
/// regain either from a saved id. The change is read back and verified, then
/// PR_SET_NO_NEW_PRIVS is applied, and finally the process is made dumpable
/// again: it is now an ordinary process of `target`, as an execve would make
/// it, and a caller that exits instead of exec'ing can be attached to by that
/// user (which the leak check of a sanitizer build needs).
///
/// The supplementary groups are left as they are: this is for a process whose
/// groups are already the caller's own.
util::expected<void, std::string> drop_privileges(ids target);

/// Run `work` on a dedicated thread that has irreversibly assumed `user`.
///
/// This exists so that a privileged, multi-threaded process can do something
/// with an unprivileged user's authority, e.g. open a file the user named,
/// without ever lowering its own credentials.
///
/// Credentials are per-task on Linux; glibc makes setuid/setgid look
/// process-wide, by broadcasting them to every thread. Bypassing that broadcast
/// keeps the change confined to this one thread, so the rest of the process
/// retains its privileges. Important because lowering a whole daemon's euid,
/// even briefly, changes the credentials of every other thread in it at the
/// same time.
///
/// The drop is one-way. Nothing inside `work` needs privilege again, and having
/// no restore path is what makes this safe to reason about: there is no window
/// to get wrong and no failure mode where the thread is left half-restored.
///
/// Returns an error if the identity could not be assumed - in which case
/// `work` does not run at all - or if `work` threw. `work` communicates its own
/// result through whatever it captures.
///
/// When the calling process is not root it cannot change its identity at all,
/// so `user` is instead required to match the identity the process already
/// has; anything else is an error. That keeps the guarantee identical in both
/// cases: `work` either runs with exactly `user`, or does not run.
util::expected<void, std::string>
run_as_user(const user_ids& user, const std::function<void()>& work);

} // namespace util
