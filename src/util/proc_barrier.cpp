#include <cstring>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/macros.h>
#include <util/proc_barrier.h>

// timeout in seconds for waiting on a barrier semaphore.
#define BARRIER_TIMEOUT 30

namespace util {

namespace {

// Wait for semaphore sem for up to timeout seconds.
util::expected<void, std::string> sem_timedwait_relative(sem_t* sem,
                                                         int timeout) {
    struct timespec deadline;

    // sem_timedwait() requires a deadline rather than a timeout.
    Z_e(clock_gettime(CLOCK_REALTIME, &deadline));
    deadline.tv_sec += timeout;
    Zfe(sem_timedwait(sem, &deadline), "failure waiting for barrier lock");
    return {};
}

} // namespace

// Enter the coordinated section and elect the leader.
util::expected<void, std::string> barrier_begin(proc_barrier& barrier,
                                                std::string tag) {
    int fd;
    barrier.sem_name = fmt::format("/uenv-run_sem-{}", tag);
    barrier.sem_done_name = fmt::format("/uenv-run_sem_ready-{}", tag);
    barrier.shm_name = fmt::format("/uenv-run_shm-{}", tag);

    // Serialize.
    barrier.sem = sem_open(barrier.sem_name.c_str(), O_CREAT, 0600, 1);
    T_e(barrier.sem != SEM_FAILED);
    // Counts how many peers have finished acting on the leader; posted once
    // by each follower, waited on by the leader.
    barrier.sem_done =
        sem_open(barrier.sem_done_name.c_str(), O_CREAT, 0600, 0);
    T_e(barrier.sem_done != SEM_FAILED);
    if (auto r = sem_timedwait_relative(barrier.sem, BARRIER_TIMEOUT); !r) {
        return r;
    }

    // Am I the leader?
    fd = shm_open(barrier.shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd > 0) {
        spdlog::trace("barrier: I am the leader! PID {}", getpid());
        barrier.is_leader = true;
        Z_e(ftruncate(fd, sizeof(*barrier.shared)));
    } else {
        std::string err{strerror(errno)};
        T_e(errno == EEXIST);
        spdlog::trace("barrier: I am a follower {}", err);
        barrier.is_leader = false;
        fd = shm_open(barrier.shm_name.c_str(), O_RDWR, 0);
        T_e(fd > 0);
    }

    barrier.shared = static_cast<decltype(barrier.shared)>(
        mmap(NULL, sizeof(*barrier.shared), PROT_READ | PROT_WRITE, MAP_SHARED,
             fd, 0));
    T__(barrier.shared != NULL);
    Z_e(close(fd));

    // Leader keeps the lock; followers parallelize (the leader will be done
    // by then).
    if (!barrier.is_leader) {
        Z_e(sem_post(barrier.sem));
    }
    return {};
}

// Leader only. Publish the pid and the peer count, then release the
// followers blocked in barrier_begin().
util::expected<void, std::string>
barrier_ready(proc_barrier& barrier, int nprocs, std::optional<pid_t> pid) {
    spdlog::trace("barrier: leader initializing shared data");
    barrier.shared->leader_pid = pid.value_or(getpid());
    barrier.shared->procs_remaining = nprocs;
    Z_e(sem_post(barrier.sem)); // release followers queued in barrier_begin
    return {};
}

// Leader only. Block until every follower has called barrier_signal_done().
util::expected<void, std::string> barrier_wait_peers(proc_barrier& barrier,
                                                     int nprocs) {
    for (int i = 0; i < nprocs - 1; ++i) {
        if (auto r = sem_timedwait_relative(barrier.sem_done, BARRIER_TIMEOUT);
            !r) {
            return r;
        }
    }
    spdlog::trace("barrier: all peers done");
    return {};
}

// Follower only. Signal that this peer is done acting on the leader.
util::expected<void, std::string> barrier_signal_done(proc_barrier& barrier) {
    Z_e(sem_post(barrier.sem_done));
    return {};
}

// Leave the coordinated section. Called by every peer (leader and
// followers) once fully done with the barrier.
util::expected<void, std::string> barrier_end(proc_barrier& barrier) {
    if (auto r = sem_timedwait_relative(barrier.sem, BARRIER_TIMEOUT); !r) {
        return r;
    }

    barrier.shared->procs_remaining--;
    spdlog::trace("barrier: {} peers left excluding myself",
                  barrier.shared->procs_remaining);

    if (barrier.shared->procs_remaining <= 0) {
        spdlog::trace("barrier: cleaning up IPC resources");
        Tf_(barrier.shared->procs_remaining == 0,
            "barrier: expected 0 peers left but found {}",
            barrier.shared->procs_remaining);
        Zfe(sem_unlink(barrier.sem_name.c_str()),
            "barrier: can't unlink sem: {}", barrier.sem_name.c_str());
        Zfe(sem_unlink(barrier.sem_done_name.c_str()),
            "barrier: can't unlink sem_done: {}",
            barrier.sem_done_name.c_str());
        Zfe(shm_unlink(barrier.shm_name.c_str()),
            "barrier: can't unlink shm: {}", barrier.shm_name.c_str());
    }

    Z_e(sem_post(barrier.sem)); // parallelize (all)

    Z_e(munmap(barrier.shared, sizeof(*barrier.shared)));
    Z_e(sem_close(barrier.sem));
    Z_e(sem_close(barrier.sem_done));

    spdlog::trace("barrier: done");
    return {};
}

} // namespace util
