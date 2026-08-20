#include <unistd.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/named_semaphore.h>
#include <util/proc_barrier.h>
#include <util/shared_mapping.h>

namespace util {

namespace {

// timeout for waiting on a barrier semaphore.
constexpr std::chrono::seconds barrier_timeout{30};

} // namespace

// The barrier's state: three IPC objects that own themselves, plus the few
// scalars describing this peer's role in the rendezvous.
class proc_barrier_impl {
  public:
    struct shared_state {
        pid_t leader_pid;
        int procs_remaining; // serial-only access, guarded by `lock`
    };

    proc_barrier_impl(bool is_leader, int nprocs, named_semaphore lock,
                      named_semaphore done, shared_mapping<shared_state> shared)
        : is_leader(is_leader), nprocs(nprocs), holds_lock(is_leader),
          lock(std::move(lock)), done(std::move(done)),
          shared(std::move(shared)) {
    }

    bool is_leader;
    int nprocs;
    bool ended = false;
    // the leader comes out of create() still holding the lock, and keeps it
    // until ready(); everyone else acquires it only inside end(). Tracked so
    // that end() -- which may be reached from the destructor on an error path
    // that never got as far as ready() -- does not wait on a lock this peer
    // already owns.
    bool holds_lock;
    named_semaphore lock; // serialises the peers
    named_semaphore done; // followers post, the leader waits
    shared_mapping<shared_state> shared;
};

// Enter the coordinated section and elect the leader.
util::expected<proc_barrier, std::string> proc_barrier::create(std::string tag,
                                                               int nprocs) {
    if (nprocs < 1) {
        return util::unexpected(
            fmt::format("proc_barrier: nprocs must be >= 1, got {}", nprocs));
    }

    using shared_state = proc_barrier_impl::shared_state;

    // Serialise.
    auto lock = named_semaphore::open(fmt::format("/uenv-run_sem-{}", tag), 1);
    if (!lock) {
        return util::unexpected(lock.error());
    }
    // Counts how many peers have finished acting on the leader; posted once
    // by each follower, waited on by the leader.
    auto done =
        named_semaphore::open(fmt::format("/uenv-run_sem_ready-{}", tag), 0);
    if (!done) {
        return util::unexpected(done.error());
    }
    if (auto r = lock->wait(barrier_timeout); !r) {
        return util::unexpected(r.error());
    }

    // Am I the leader? Whoever creates the shared memory object wins; the
    // rest find it already there.
    const auto shm_name = fmt::format("/uenv-run_shm-{}", tag);
    auto created = shared_mapping<shared_state>::create_exclusive(shm_name);
    if (!created) {
        return util::unexpected(created.error());
    }

    const bool is_leader = created->has_value();
    shared_mapping<shared_state> shared;
    if (is_leader) {
        spdlog::trace("barrier: set as leader PID {}", getpid());
        shared = std::move(**created);
        // seed the peer count here rather than in ready(), so that a leader
        // that fails before it gets that far still leaves a count the
        // remaining peers can decrement to zero and clean up behind.
        shared->procs_remaining = nprocs;
        shared->leader_pid = 0;
    } else {
        spdlog::trace("barrier: set as follower");
        auto opened = shared_mapping<shared_state>::open_existing(shm_name);
        if (!opened) {
            return util::unexpected(opened.error());
        }
        shared = std::move(*opened);

        // Leader keeps the lock; followers parallelize (the leader will be
        // done by then).
        if (auto r = lock->post(); !r) {
            return util::unexpected(r.error());
        }
    }

    return proc_barrier{std::make_unique<proc_barrier_impl>(
        is_leader, nprocs, std::move(*lock), std::move(*done),
        std::move(shared))};
}

proc_barrier::proc_barrier(std::unique_ptr<proc_barrier_impl> impl)
    : impl_(std::move(impl)) {
}

proc_barrier::proc_barrier(proc_barrier&&) noexcept = default;
proc_barrier& proc_barrier::operator=(proc_barrier&&) noexcept = default;

// Leaves the coordinated section if the caller did not. The IPC handles
// themselves are closed by impl_'s members.
proc_barrier::~proc_barrier() {
    if (impl_ && !impl_->ended) {
        if (auto r = end(); !r) {
            spdlog::warn("barrier: end failed during cleanup: {}", r.error());
        }
    }
}

bool proc_barrier::is_leader() const {
    return impl_->is_leader;
}

pid_t proc_barrier::leader_pid() const {
    return impl_->shared->leader_pid;
}

// Leader only. Publish the pid, then release the followers blocked in
// create().
util::expected<void, std::string>
proc_barrier::ready(std::optional<pid_t> pid) {
    spdlog::trace("barrier: leader initializing shared data");
    impl_->shared->leader_pid = pid.value_or(getpid());
    // release the followers queued in create()
    if (auto r = impl_->lock.post(); !r) {
        return r;
    }
    impl_->holds_lock = false;
    return {};
}

// Leader only. Block until every follower has called signal_done().
util::expected<void, std::string> proc_barrier::wait_peers() {
    for (int i = 0; i < impl_->nprocs - 1; ++i) {
        if (auto r = impl_->done.wait(barrier_timeout); !r) {
            return r;
        }
    }
    spdlog::trace("barrier: all peers done");
    return {};
}

// Follower only. Signal that this peer is done acting on the leader.
util::expected<void, std::string> proc_barrier::signal_done() {
    return impl_->done.post();
}

// Leave the coordinated section. Called by every peer (leader and followers)
// once fully done with the barrier. This is the protocol half of the teardown:
// the handles are closed by the destructors of impl_'s members, so an error
// here leaks nothing.
util::expected<void, std::string> proc_barrier::end() {
    if (impl_->ended) {
        return {};
    }
    // set before doing anything, so that neither a failure below nor the
    // destructor can run the protocol a second time.
    impl_->ended = true;

    if (!impl_->holds_lock) {
        if (auto r = impl_->lock.wait(barrier_timeout); !r) {
            return r;
        }
        impl_->holds_lock = true;
    }

    impl_->shared->procs_remaining--;
    spdlog::trace("barrier: {} peers left excluding myself",
                  impl_->shared->procs_remaining);

    if (impl_->shared->procs_remaining <= 0) {
        spdlog::trace("barrier: cleaning up IPC resources");
        if (impl_->shared->procs_remaining != 0) {
            return util::unexpected(
                fmt::format("barrier: expected 0 peers left but found {}",
                            impl_->shared->procs_remaining));
        }
        if (auto r = impl_->lock.unlink(); !r) {
            return r;
        }
        if (auto r = impl_->done.unlink(); !r) {
            return r;
        }
        if (auto r = impl_->shared.unlink(); !r) {
            return r;
        }
    }

    if (auto r = impl_->lock.post(); !r) { // parallelize (all)
        return r;
    }
    impl_->holds_lock = false;

    spdlog::trace("barrier: done");
    return {};
}

} // namespace util
