#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <vector>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/ns_join.h>

namespace {

constexpr int max_tasks = 8;

// shared, cross-process record of when each task finished its part of the
// join, indexed by the task's position in the fork loop (known up front, so
// there's no need to work out post-hoc who won the race).
struct timeline {
    struct timespec ts[max_tasks];
    bool is_winner[max_tasks];
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

// join_wait_peers() is the barrier that lets a winner safely do something
// (like prctl(PR_SET_DUMPABLE, 0)) that would break the other tasks' access
// to its /proc/<pid>/ns/* only *after* every other task has finished using
// them. This test checks that happens-before relationship directly, without
// touching real namespaces (namespaces_join()/setns() needs privileges this
// test environment may not have).
TEST_CASE("join barrier orders winner after all losers", "[ns_join]") {
    const int ntasks = 4;
    REQUIRE(ntasks <= max_tasks);

    auto* tl = static_cast<timeline*>(mmap(nullptr, sizeof(timeline),
                                           PROT_READ | PROT_WRITE,
                                           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    REQUIRE(tl != MAP_FAILED);
    *tl = timeline{};

    const auto join_tag = fmt::format("unittest-{}", getpid());

    std::vector<pid_t> children;
    for (int i = 0; i < ntasks; ++i) {
        pid_t pid = fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            uenv::join_t join;
            auto r = uenv::join_begin(join, join_tag);
            if (!r) {
                _exit(1);
            }

            if (join.winner_p) {
                // simulate mount setup taking a while, so a winner that
                // doesn't wait for peers would very likely record its
                // timestamp before at least one (staggered) loser.
                sleep_ms(50);

                if (auto rr = uenv::join_ready(join, ntasks, std::nullopt);
                    !rr) {
                    _exit(2);
                }
                if (auto rr = uenv::join_wait_peers(join, ntasks); !rr) {
                    _exit(3);
                }
                tl->is_winner[i] = true;
                record(tl, i);
            } else {
                // stagger losers so a broken barrier fails reliably instead
                // of flaking.
                sleep_ms(20 * i);
                record(tl, i);
                if (auto rr = uenv::join_signal_done(join); !rr) {
                    _exit(4);
                }
            }

            if (auto rr = uenv::join_end(join); !rr) {
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

    int winner_idx = -1;
    for (int i = 0; i < ntasks; ++i) {
        REQUIRE(tl->set[i]);
        if (tl->is_winner[i]) {
            REQUIRE(winner_idx == -1); // exactly one winner
            winner_idx = i;
        }
    }
    REQUIRE(winner_idx != -1);

    const auto& winner_ts = tl->ts[winner_idx];
    for (int i = 0; i < ntasks; ++i) {
        if (i == winner_idx) {
            continue;
        }
        // winner's timestamp (recorded after join_wait_peers returned) must
        // not be earlier than any loser's (recorded before join_signal_done).
        const auto& loser_ts = tl->ts[i];
        bool winner_after_loser = (winner_ts.tv_sec > loser_ts.tv_sec) ||
                                  (winner_ts.tv_sec == loser_ts.tv_sec &&
                                   winner_ts.tv_nsec >= loser_ts.tv_nsec);
        REQUIRE(winner_after_loser);
    }

    munmap(tl, sizeof(timeline));
}
