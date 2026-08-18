#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <util/proc_barrier.h>

namespace {

constexpr int max_tasks = 8;

// shared, cross-process record of when each task finished its part of the
// rendezvous, indexed by the task's position in the fork loop (known up
// front, so there's no need to work out post-hoc who won the race).
struct timeline {
    struct timespec ts[max_tasks];
    bool is_leader[max_tasks];
    bool set[max_tasks];
};

// shared, cross-process record of the pid each follower read out of the
// barrier, indexed the same way.
struct pid_report {
    pid_t observed[max_tasks];
    bool is_leader[max_tasks];
    bool set[max_tasks];
};

template <typename T> T* shared_alloc() {
    auto* p = static_cast<T*>(mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE,
                                   MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    REQUIRE(p != MAP_FAILED);
    *p = T{};
    return p;
}

void sleep_ms(int ms) {
    struct timespec t{ms / 1000, (ms % 1000) * 1000000};
    nanosleep(&t, nullptr);
}

void record(timeline* tl, int idx) {
    clock_gettime(CLOCK_MONOTONIC, &tl->ts[idx]);
    tl->set[idx] = true;
}

// fork `ntasks` children, run `body(i)` in each, and require that they all
// exit with status 0.
template <typename Body> void run_tasks(int ntasks, Body&& body) {
    std::vector<pid_t> children;
    for (int i = 0; i < ntasks; ++i) {
        pid_t pid = fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            body(i);
            _exit(0);
        }
        children.push_back(pid);
    }

    for (auto pid : children) {
        int status = 0;
        REQUIRE(waitpid(pid, &status, 0) == pid);
        INFO("child pid " << pid);
        REQUIRE(WIFEXITED(status));
        REQUIRE(WEXITSTATUS(status) == 0);
    }
}

} // namespace

// wait_peers() lets the leader safely do something (like
// prctl(PR_SET_DUMPABLE, 0)) that would break the other tasks' access to its
// /proc/<pid>/ns/* only *after* every other task has finished using them.
// This test checks that happens-before relationship directly, without
// touching real namespaces (namespaces_join()/setns() needs privileges this
// test environment may not have).
TEST_CASE("barrier orders leader after all followers", "[proc_barrier]") {
    const int ntasks = 4;
    REQUIRE(ntasks <= max_tasks);

    auto* tl = shared_alloc<timeline>();
    const auto barrier_tag = fmt::format("unittest-{}", getpid());

    run_tasks(ntasks, [&](int i) {
        auto barrier = util::proc_barrier::create(barrier_tag, ntasks);
        if (!barrier) {
            _exit(1);
        }

        if (barrier->is_leader()) {
            // simulate mount setup taking a while, so a leader that doesn't
            // wait for peers would very likely record its timestamp before at
            // least one (staggered) follower.
            sleep_ms(50);

            if (auto rr = barrier->ready(); !rr) {
                _exit(2);
            }
            if (auto rr = barrier->wait_peers(); !rr) {
                _exit(3);
            }
            tl->is_leader[i] = true;
            record(tl, i);
        } else {
            // stagger followers so a broken barrier fails reliably instead of
            // flaking.
            sleep_ms(20 * i);
            record(tl, i);
            if (auto rr = barrier->signal_done(); !rr) {
                _exit(4);
            }
        }

        if (auto rr = barrier->end(); !rr) {
            _exit(5);
        }
    });

    int leader_idx = -1;
    for (int i = 0; i < ntasks; ++i) {
        REQUIRE(tl->set[i]);
        if (tl->is_leader[i]) {
            REQUIRE(leader_idx == -1); // exactly one leader
            leader_idx = i;
        }
    }
    REQUIRE(leader_idx != -1);

    const auto& leader_ts = tl->ts[leader_idx];
    for (int i = 0; i < ntasks; ++i) {
        if (i == leader_idx) {
            continue;
        }
        // the leader's timestamp (recorded after wait_peers returned) must
        // not be earlier than any follower's (recorded before signal_done).
        const auto& follower_ts = tl->ts[i];
        bool leader_after_follower =
            (leader_ts.tv_sec > follower_ts.tv_sec) ||
            (leader_ts.tv_sec == follower_ts.tv_sec &&
             leader_ts.tv_nsec >= follower_ts.tv_nsec);
        REQUIRE(leader_after_follower);
    }

    munmap(tl, sizeof(timeline));
}

// the Slurm plugin's squashfs-mount path publishes the pid of a *forked
// child* rather than the leader's own pid, so that the other tasks join the
// namespace that child created. Check that followers see exactly the pid the
// leader passed to ready().
TEST_CASE("barrier publishes an explicit pid", "[proc_barrier]") {
    const int ntasks = 4;
    REQUIRE(ntasks <= max_tasks);

    auto* rep = shared_alloc<pid_report>();
    const auto barrier_tag = fmt::format("unittest-pid-{}", getpid());

    // an arbitrary pid that is not any of the participating processes.
    const pid_t published = 424242;

    run_tasks(ntasks, [&](int i) {
        auto barrier = util::proc_barrier::create(barrier_tag, ntasks);
        if (!barrier) {
            _exit(1);
        }

        if (barrier->is_leader()) {
            rep->is_leader[i] = true;
            rep->set[i] = true;
            if (auto rr = barrier->ready(published); !rr) {
                _exit(2);
            }
            if (auto rr = barrier->wait_peers(); !rr) {
                _exit(3);
            }
        } else {
            rep->observed[i] = barrier->leader_pid();
            rep->set[i] = true;
            if (auto rr = barrier->signal_done(); !rr) {
                _exit(4);
            }
        }

        if (auto rr = barrier->end(); !rr) {
            _exit(5);
        }
    });

    int followers = 0;
    for (int i = 0; i < ntasks; ++i) {
        REQUIRE(rep->set[i]);
        if (!rep->is_leader[i]) {
            REQUIRE(rep->observed[i] == published);
            ++followers;
        }
    }
    REQUIRE(followers == ntasks - 1);

    munmap(rep, sizeof(pid_report));
}

// a barrier that goes out of scope without end() having been called must
// still leave the IPC objects unlinked -- that is what stops an error path
// between create() and end() from leaking a semaphore or shm segment onto the
// node.
TEST_CASE("barrier destructor releases the IPC objects", "[proc_barrier]") {
    const auto tag = fmt::format("unittest-dtor-{}", getpid());
    const auto sem_name = fmt::format("/uenv-run_sem-{}", tag);
    const auto sem_done_name = fmt::format("/uenv-run_sem_ready-{}", tag);
    const auto shm_name = fmt::format("/uenv-run_shm-{}", tag);

    {
        // a single peer, so this process is the leader and the same process
        // takes the count to zero.
        auto barrier = util::proc_barrier::create(tag, 1);
        REQUIRE(barrier);
        REQUIRE(barrier->is_leader());
        REQUIRE(barrier->ready());

        // the objects exist while the barrier is alive.
        sem_t* s = sem_open(sem_name.c_str(), 0);
        REQUIRE(s != SEM_FAILED);
        sem_close(s);

        // ... and no end() call here: the destructor has to do it.
    }

    REQUIRE(sem_open(sem_name.c_str(), 0) == SEM_FAILED);
    REQUIRE(errno == ENOENT);
    REQUIRE(sem_open(sem_done_name.c_str(), 0) == SEM_FAILED);
    REQUIRE(errno == ENOENT);
    REQUIRE(shm_open(shm_name.c_str(), O_RDWR, 0) == -1);
    REQUIRE(errno == ENOENT);
}

// the leader comes out of create() still holding the barrier lock, and only
// releases it in ready(). A leader that fails before then -- a mount error,
// say -- must still be able to tear the barrier down rather than waiting out
// the full timeout on a lock it owns itself.
TEST_CASE("barrier cleans up when the leader never calls ready",
          "[proc_barrier]") {
    const auto tag = fmt::format("unittest-noready-{}", getpid());
    const auto sem_name = fmt::format("/uenv-run_sem-{}", tag);
    const auto shm_name = fmt::format("/uenv-run_shm-{}", tag);

    struct timespec before {};
    struct timespec after {};
    clock_gettime(CLOCK_MONOTONIC, &before);
    {
        auto barrier = util::proc_barrier::create(tag, 1);
        REQUIRE(barrier);
        REQUIRE(barrier->is_leader());
        // no ready(), no end(): straight out of scope.
    }
    clock_gettime(CLOCK_MONOTONIC, &after);

    // must not have waited out the 30s barrier timeout.
    REQUIRE(after.tv_sec - before.tv_sec < 5);

    REQUIRE(sem_open(sem_name.c_str(), 0) == SEM_FAILED);
    REQUIRE(errno == ENOENT);
    REQUIRE(shm_open(shm_name.c_str(), O_RDWR, 0) == -1);
    REQUIRE(errno == ENOENT);
}

// end() is idempotent: calling it explicitly and then letting the destructor
// run must not decrement the peer count twice or double-unlink.
TEST_CASE("barrier end is idempotent", "[proc_barrier]") {
    const auto tag = fmt::format("unittest-end-{}", getpid());

    auto barrier = util::proc_barrier::create(tag, 1);
    REQUIRE(barrier);
    REQUIRE(barrier->ready());
    REQUIRE(barrier->end());
    REQUIRE(barrier->end());
}
