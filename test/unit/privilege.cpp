#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include <util/privilege.h>

namespace {

// Every child branch below terminates with _exit() rather than return/throw:
// a forked child shares the parent's Catch2 runner state, so unwinding
// normally back into it would re-enter the test machinery inside the child.

std::vector<gid_t> own_groups() {
    const int n = getgroups(0, nullptr);
    REQUIRE(n >= 0);
    std::vector<gid_t> groups(static_cast<std::size_t>(n));
    if (n > 0) {
        REQUIRE(getgroups(n, groups.data()) == n);
    }
    return groups;
}

} // namespace

//
// reading ids
//

TEST_CASE("current_ids agrees with the libc getters", "[privilege]") {
    auto ids = util::current_ids();
    REQUIRE(ids.has_value());
    REQUIRE(ids->real.uid == getuid());
    REQUIRE(ids->effective.uid == geteuid());
    REQUIRE(ids->real.gid == getgid());
    REQUIRE(ids->effective.gid == getegid());
    REQUIRE(ids->groups == own_groups());
}

TEST_CASE("current_user is the real identity plus the groups", "[privilege]") {
    auto ids = util::current_ids();
    auto user = util::current_user();
    REQUIRE(ids.has_value());
    REQUIRE(user.has_value());
    REQUIRE(user->id == ids->real);
    REQUIRE(user->groups == ids->groups);
}

TEST_CASE("verify_ids accepts the current identity", "[privilege]") {
    auto ids = util::current_ids();
    REQUIRE(ids.has_value());
    if (ids->real != ids->effective || ids->real != ids->saved) {
        SKIP("the real, effective and saved ids differ");
    }
    REQUIRE(util::verify_ids(ids->real).has_value());
}

TEST_CASE("verify_ids reports a mismatch", "[privilege]") {
    auto ids = util::current_ids();
    REQUIRE(ids.has_value());

    auto wrong = ids->real;
    wrong.uid = wrong.uid + 1;
    auto result = util::verify_ids(wrong);
    REQUIRE(!result.has_value());
    // the message names the observed ids and the expected one
    REQUIRE(result.error().find(std::to_string(ids->real.uid)) !=
            std::string::npos);
    REQUIRE(result.error().find(std::to_string(wrong.uid)) !=
            std::string::npos);
}

//
// process-wide changes
//

TEST_CASE("set_effective_uid to the current uid succeeds", "[privilege]") {
    const auto before = util::current_ids();
    REQUIRE(before.has_value());

    REQUIRE(util::set_effective_uid(geteuid()).has_value());
    REQUIRE(util::current_ids() == before);
}

TEST_CASE("become_root fails without a saved root uid", "[privilege]") {
    const auto before = util::current_ids();
    REQUIRE(before.has_value());
    if (before->saved.uid == 0 || before->effective.uid == 0) {
        SKIP("the process can become root");
    }

    auto result = util::become_root();
    REQUIRE(!result.has_value());
    // a failed attempt leaves the identity as it was
    REQUIRE(util::current_ids() == before);
}

TEST_CASE("drop_privileges sets all ids and no_new_privs", "[privilege]") {
    const auto ids = util::current_ids();
    REQUIRE(ids.has_value());

    // PR_SET_NO_NEW_PRIVS is irreversible, so the change is made in a child.
    // Exit codes name the failing step.
    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        if (!util::drop_privileges(ids->real)) {
            _exit(1);
        }
        auto after = util::current_ids();
        if (!after) {
            _exit(2);
        }
        if (after->real != ids->real || after->effective != ids->real ||
            after->saved != ids->real) {
            _exit(3);
        }
        if (prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) {
            _exit(4);
        }
        _exit(0);
    }

    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    // the parent is untouched
    REQUIRE(util::current_ids() == ids);
}

//
// run_as_user
//

TEST_CASE("run_as_user runs the work", "[privilege]") {
    const auto before = util::current_ids();
    REQUIRE(before.has_value());

    bool ran = false;
    uid_t seen_uid = static_cast<uid_t>(-1);
    auto result = util::run_as_user(util::current_user().value(), [&] {
        ran = true;
        seen_uid = geteuid();
    });

    REQUIRE(result.has_value());
    REQUIRE(ran);
    REQUIRE(seen_uid == getuid());
    // run_as_user changes the ids of one thread only, so the process itself
    // must be identical before and after every call - that is the guarantee.
    REQUIRE(util::current_ids() == before);
}

TEST_CASE("run_as_user reports a failure to assume the identity",
          "[privilege]") {
    if (geteuid() == 0) {
        SKIP("running as root: another user's identity can be assumed");
    }
    const auto before = util::current_ids();
    REQUIRE(before.has_value());

    // an unprivileged process cannot become anybody else
    auto user = util::current_user().value();
    user.id.uid = user.id.uid + 1;

    bool ran = false;
    auto result = util::run_as_user(user, [&] { ran = true; });

    REQUIRE(!result.has_value());
    // the work must not run with the wrong identity ...
    REQUIRE(!ran);
    // ... and the process must be left exactly as it was.
    REQUIRE(util::current_ids() == before);
}

TEST_CASE("run_as_user confines the identity to one thread", "[privilege]") {
    if (geteuid() != 0) {
        SKIP("only root can assume another user's identity");
    }
    const auto before = util::current_ids();
    REQUIRE(before.has_value());

    // This is the guarantee the Slurm plugin depends on: the work runs as an
    // unprivileged user while the process - slurmstepd - stays root. If the
    // change were process-wide, as glibc's setuid would make it, the check on
    // the process ids below would fail.
    const util::user_ids nobody{.id = {.uid = 65534, .gid = 65534},
                                .groups = {65534}};

    uid_t seen_uid = 0;
    gid_t seen_gid = 0;
    auto result = util::run_as_user(nobody, [&] {
        seen_uid = geteuid();
        seen_gid = getegid();
    });

    REQUIRE(result.has_value());
    REQUIRE(seen_uid == nobody.id.uid);
    REQUIRE(seen_gid == nobody.id.gid);
    REQUIRE(util::current_ids() == before);
    REQUIRE(geteuid() == 0);
}

TEST_CASE("run_as_user does not let an exception escape", "[privilege]") {
    const auto before = util::current_ids();
    REQUIRE(before.has_value());

    // an exception crossing back out would terminate slurmstepd.
    auto result = util::run_as_user(util::current_user().value(), [] {
        throw std::runtime_error("from the worker thread");
    });

    REQUIRE(!result.has_value());
    REQUIRE(result.error().find("from the worker thread") != std::string::npos);
    REQUIRE(util::current_ids() == before);
}
