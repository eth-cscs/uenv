#pragma once

#include <semaphore.h>
#include <sys/types.h>

#include <optional>
#include <string>

#include <util/expected.h>

namespace util {

// A rendezvous for a group of unrelated processes that independently reach
// the same point: exactly one is elected leader and performs some setup
// while the rest wait, and the last one out releases the shared resources.
//
// The barrier knows nothing about what the setup is. The leader publishes a
// single pid -- its own, or any other it chooses -- which is all the
// followers get told about it.
//
// Peers find each other by tag: everyone sharing a tag joins the same
// barrier. The POSIX semaphores and shared memory the barrier is built from
// are named after the tag, so it must be unique to the group (a job and step
// id, for example).
//
// Protocol:
//
//     barrier_begin()            all peers; elects the leader
//     if (barrier.is_leader) {
//         ... setup ...
//         barrier_ready()        publish the pid, release the followers
//         barrier_wait_peers()   returns once every follower is done
//         ... work that would break followers if done any earlier ...
//     } else {
//         ... act on barrier.leader_pid() ...
//         barrier_signal_done()
//     }
//     barrier_end()              all peers; last one unlinks the IPC objects
struct proc_barrier {
    bool is_leader;
    std::string sem_name;
    sem_t* sem;
    std::string sem_done_name;
    sem_t* sem_done;
    std::string shm_name;
    struct shared_state {
        pid_t leader_pid;
        int procs_remaining; // serial-only access
    }* shared;

    // The pid published by the leader in barrier_ready(). Only meaningful to
    // a follower once barrier_begin() has returned.
    pid_t leader_pid() const {
        return shared->leader_pid;
    }
};

// All peers. Elects the leader: on return exactly one peer has is_leader
// set. Followers stay blocked here until the leader calls barrier_ready().
util::expected<void, std::string> barrier_begin(proc_barrier& barrier,
                                                std::string tag);

// Leader only. Publish pid (its own, if nullopt) along with the peer count,
// then release the followers blocked in barrier_begin() so they can act on
// the leader while it is still in the state they need. Must be called before
// anything that would invalidate that state.
util::expected<void, std::string>
barrier_ready(proc_barrier& barrier, int nprocs, std::optional<pid_t> pid);

// Leader only. Block until every follower has called barrier_signal_done().
util::expected<void, std::string> barrier_wait_peers(proc_barrier& barrier,
                                                     int nprocs);

// Follower only. Signal that this peer is done acting on the leader.
util::expected<void, std::string> barrier_signal_done(proc_barrier& barrier);

// Called by every peer (leader and followers) once fully done with the
// barrier. Decrements the shared peer counter; whichever call brings it to 0
// unlinks the IPC objects.
util::expected<void, std::string> barrier_end(proc_barrier& barrier);

} // namespace util
