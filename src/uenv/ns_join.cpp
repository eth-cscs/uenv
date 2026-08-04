#include "ns_join.h"
#include "macros.h"
#include <cstring>
#include <fcntl.h>
#include <semaphore.h>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/mman.h>
#include <util/expected.h>

// timeout in seconds for waiting for join semaphore.
#define JOIN_TIMEOUT 30

namespace uenv {
// Join a specific namespace.
util::expected<void, std::string> namespace_join(pid_t pid,
                                                 const std::string& ns) {
    std::string path;
    int fd;

    path = fmt::format("/proc/{}/ns/{}", pid, ns);
    fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT)
            return util::unexpected(
                fmt::format("join: no PID {}: {} not found", pid, path));
        else
            return util::unexpected(fmt::format("join: can't open {}", path));
    }
    // setns(2) seems to be involved in some kind of race with syslog(3).
    // Rarely, when configured with --enable-syslog, the call fails with
    // EINVAL. We never figured out a proper fix, so just retry a few times in
    // a loop. See issue https://github.com/hpc/charliecloud/issues/1270.
    for (int i = 1; setns(fd, 0) != 0; i++)
        if (i >= 5) {
            return util::unexpected(
                fmt::format("can’t join {} namespace of pid {}", ns, pid));
        } else {
            spdlog::warn("can’t join {} namespace; trying again, {}", ns,
                         strerror(errno));
            sleep(1);
        }
    return {};
}

// Join the existing namespaces containing process pid, which could be the
// join winner or another process.
util::expected<void, std::string>
namespaces_join(pid_t pid, const std::vector<std::string>& ns_names) {
    for (auto& ns : ns_names) {
        spdlog::warn("joining namespace {} of pid {}", ns, pid);
        auto r = namespace_join(pid, ns);
        if (!r) {
            return r;
        }
    }
    return {};
}

// Wait for semaphore sem for up to timeout seconds. If timeout or an error,
// exit unsuccessfully.
util::expected<void, std::string> sem_timedwait_relative(sem_t* sem,
                                                         int timeout) {
    struct timespec deadline;

    // sem_timedwait() requires a deadline rather than a timeout.
    Z_e(clock_gettime(CLOCK_REALTIME, &deadline));
    deadline.tv_sec += timeout;
    Zfe(sem_timedwait(sem, &deadline), "failure waiting for join lock");
    return {};
}

// Begin coordinated section of namespace joining.
util::expected<void, std::string> join_begin(join_t& join,
                                             std::string join_tag) {
    int fd;
    join.sem_name = fmt::format("/uenv-run_sem-{}", join_tag);
    join.shm_name = fmt::format("/uenv-run_shm-{}", join_tag);

    // Serialize.
    join.sem = sem_open(join.sem_name.c_str(), O_CREAT, 0600, 1);
    T_e(join.sem != SEM_FAILED);
    if (auto r = sem_timedwait_relative(join.sem, JOIN_TIMEOUT); !r)
        return r;

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
    return {};
}

// End coordinated section of namespace joining.
util::expected<void, std::string> join_end(join_t& join, int join_ct,
                                           std::optional<pid_t> winner_pid) {
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
    return {};
}

} // namespace uenv
