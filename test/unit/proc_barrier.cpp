#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

void sleep_ms(int ms) {
    struct timespec t{ms / 1000, (ms % 1000) * 1000000};
    nanosleep(&t, nullptr);
}

void record(timeline* tl, int idx) {
    clock_gettime(CLOCK_MONOTONIC, &tl->ts[idx]);
    tl->set[idx] = true;
}

} // namespace

// barrier_wait_peers() lets the leader safely do something (like
// prctl(PR_SET_DUMPABLE, 0)) that would break the other tasks' access to its
// /proc/<pid>/ns/* only *after* every other task has finished using them.
// This test checks that happens-before relationship directly, without
// touching real namespaces (namespaces_join()/setns() needs privileges this
// test environment may not have).
TEST_CASE("barrier orders leader after all followers", "[proc_barrier]") {
    const int ntasks = 4;
    REQUIRE(ntasks <= max_tasks);

    auto* tl = static_cast<timeline*>(mmap(nullptr, sizeof(timeline),
                                           PROT_READ | PROT_WRITE,
                                           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    REQUIRE(tl != MAP_FAILED);
    *tl = timeline{};

    const auto barrier_tag = fmt::format("unittest-{}", getpid());

    std::vector<pid_t> children;
    for (int i = 0; i < ntasks; ++i) {
        pid_t pid = fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            util::proc_barrier barrier;
            auto r = util::barrier_begin(barrier, barrier_tag);
            if (!r) {
                _exit(1);
            }

            if (barrier.is_leader) {
                // simulate mount setup taking a while, so a leader that
                // doesn't wait for peers would very likely record its
                // timestamp before at least one (staggered) follower.
                sleep_ms(50);

                if (auto rr =
                        util::barrier_ready(barrier, ntasks, std::nullopt);
                    !rr) {
                    _exit(2);
                }
                if (auto rr = util::barrier_wait_peers(barrier, ntasks); !rr) {
                    _exit(3);
                }
                tl->is_leader[i] = true;
                record(tl, i);
            } else {
                // stagger followers so a broken barrier fails reliably
                // instead of flaking.
                sleep_ms(20 * i);
                record(tl, i);
                if (auto rr = util::barrier_signal_done(barrier); !rr) {
                    _exit(4);
                }
            }

            if (auto rr = util::barrier_end(barrier); !rr) {
                _exit(5);
            }
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
        // the leader's timestamp (recorded after barrier_wait_peers
        // returned) must not be earlier than any follower's (recorded before
        // barrier_signal_done).
        const auto& follower_ts = tl->ts[i];
        bool leader_after_follower = (leader_ts.tv_sec > follower_ts.tv_sec) ||
                                     (leader_ts.tv_sec == follower_ts.tv_sec &&
                                      leader_ts.tv_nsec >= follower_ts.tv_nsec);
        REQUIRE(leader_after_follower);
    }

    munmap(tl, sizeof(timeline));
}
