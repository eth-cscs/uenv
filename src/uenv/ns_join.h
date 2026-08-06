#pragma once
#include <optional>
#include <semaphore.h>
#include <string>
#include <util/expected.h>
#include <vector>

namespace uenv {

/* Variables for coordinating join */
struct join_t {
    bool winner_p;
    std::string sem_name;
    sem_t* sem;
    std::string sem_ready_name;
    sem_t* sem_ready;
    std::string shm_name;
    struct {
        pid_t winner_pid;
        int proc_left_ct; // serial-only access
    }* shared;
};

util::expected<void, std::string> join_begin(join_t& join,
                                             std::string join_tag);

// Winner only. Publish winner_pid and the peer count, then release the
// losers blocked in join_begin() so they can call namespaces_join() while
// the winner is still ptrace-accessible. Must be called before anything
// that could make the winner non-dumpable.
util::expected<void, std::string> join_ready(join_t& join, int join_ct,
                                             std::optional<pid_t> winner_pid);

// Winner only. Block until every loser has called join_signal_done(). Call
// this before prctl(PR_SET_DUMPABLE, 0) or anything else that would make
// /proc/<pid>/ns/* inaccessible to the other tasks.
util::expected<void, std::string> join_wait_peers(join_t& join, int join_ct);

// Loser only. Signal that this task is done using the winner's namespaces.
util::expected<void, std::string> join_signal_done(join_t& join);

// Called by every task (winner and losers) once fully done with the join.
// Decrements the shared peer counter; whichever call brings it to 0 unlinks
// the IPC objects.
util::expected<void, std::string> join_end(join_t& join);

util::expected<void, std::string>
namespaces_join(pid_t pid, const std::vector<std::string>& ns_names);

} // namespace uenv
