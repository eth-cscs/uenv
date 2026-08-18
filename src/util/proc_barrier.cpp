#include <cstring>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/macros.h>
#include <util/proc_barrier.h>

namespace util {

namespace {

// timeout in seconds for waiting on a barrier semaphore.
constexpr int barrier_timeout = 30;

//
// RAII wrappers over the POSIX IPC objects the barrier is built from. Both
// follow the util::file_lock pattern in util/fs.h: a private handle, a static
// factory returning expected, deleted copy, a move that steals and nulls the
// handle, and an idempotent release() run by the destructor.
//
// Closing and unlinking are separate operations: every peer closes its own
// handle when the barrier is destroyed, but only the last peer out unlinks the
// name, so unlink() is explicit.
//

// A POSIX named semaphore.
class named_semaphore {
  public:
    named_semaphore() = default;

    named_semaphore(const named_semaphore&) = delete;
    named_semaphore& operator=(const named_semaphore&) = delete;

    named_semaphore(named_semaphore&& other) noexcept
        : name_(std::move(other.name_)), sem_(other.sem_) {
        other.sem_ = SEM_FAILED;
    }

    named_semaphore& operator=(named_semaphore&& other) noexcept {
        if (this != &other) {
            release();
            name_ = std::move(other.name_);
            sem_ = other.sem_;
            other.sem_ = SEM_FAILED;
        }
        return *this;
    }

    ~named_semaphore() {
        release();
    }

    // open (creating if required) the named semaphore, initialising it to
    // `value` if this call is the one that creates it.
    static util::expected<named_semaphore, std::string>
    open(std::string name, unsigned value) {
        sem_t* sem = sem_open(name.c_str(), O_CREAT, 0600, value);
        if (sem == SEM_FAILED) {
            return util::unexpected(fmt::format(
                "unable to open semaphore {}: {}", name, strerror(errno)));
        }
        return named_semaphore{std::move(name), sem};
    }

    const std::string& name() const {
        return name_;
    }

    // wait for up to `timeout` seconds.
    util::expected<void, std::string> wait(int timeout) {
        timespec deadline{};

        // sem_timedwait() requires a deadline rather than a timeout.
        Z_e(clock_gettime(CLOCK_REALTIME, &deadline));
        deadline.tv_sec += timeout;
        Zfe(sem_timedwait(sem_, &deadline), "failure waiting for barrier lock");
        return {};
    }

    util::expected<void, std::string> post() {
        Z_e(sem_post(sem_));
        return {};
    }

    // remove the name from the system; existing handles stay usable.
    util::expected<void, std::string> unlink() {
        Zfe(sem_unlink(name_.c_str()), "barrier: can't unlink sem: {}", name_);
        return {};
    }

  private:
    named_semaphore(std::string name, sem_t* sem)
        : name_(std::move(name)), sem_(sem) {
    }

    void release() {
        if (sem_ != SEM_FAILED) {
            sem_close(sem_);
            sem_ = SEM_FAILED;
        }
    }

    std::string name_;
    sem_t* sem_ = SEM_FAILED;
};

// A POSIX shared memory object holding a single T, mapped into this process.
template <typename T> class shared_mapping {
  public:
    shared_mapping() = default;

    shared_mapping(const shared_mapping&) = delete;
    shared_mapping& operator=(const shared_mapping&) = delete;

    shared_mapping(shared_mapping&& other) noexcept
        : name_(std::move(other.name_)), data_(other.data_) {
        other.data_ = nullptr;
    }

    shared_mapping& operator=(shared_mapping&& other) noexcept {
        if (this != &other) {
            release();
            name_ = std::move(other.name_);
            data_ = other.data_;
            other.data_ = nullptr;
        }
        return *this;
    }

    ~shared_mapping() {
        release();
    }

    // create the shared memory object. Returns nullopt -- which is a success,
    // not an error -- if another peer created it first; that race is how the
    // leader is elected.
    static util::expected<std::optional<shared_mapping>, std::string>
    create_exclusive(std::string name) {
        int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) {
            if (errno == EEXIST) {
                return std::optional<shared_mapping>{};
            }
            return util::unexpected(fmt::format("unable to create shm {}: {}",
                                                name, strerror(errno)));
        }
        if (ftruncate(fd, sizeof(T)) != 0) {
            std::string err{strerror(errno)};
            close(fd);
            return util::unexpected(
                fmt::format("unable to size shm {}: {}", name, err));
        }
        auto mapped = map(std::move(name), fd);
        if (!mapped) {
            return util::unexpected(mapped.error());
        }
        return std::optional<shared_mapping>{std::move(*mapped)};
    }

    // open a shared memory object created by another peer.
    static util::expected<shared_mapping, std::string>
    open_existing(std::string name) {
        int fd = shm_open(name.c_str(), O_RDWR, 0);
        if (fd < 0) {
            return util::unexpected(fmt::format("unable to open shm {}: {}",
                                                name, strerror(errno)));
        }
        return map(std::move(name), fd);
    }

    const std::string& name() const {
        return name_;
    }

    T* operator->() const {
        return data_;
    }

    util::expected<void, std::string> unlink() {
        Zfe(shm_unlink(name_.c_str()), "barrier: can't unlink shm: {}", name_);
        return {};
    }

  private:
    shared_mapping(std::string name, T* data)
        : name_(std::move(name)), data_(data) {
    }

    // map the whole object and close fd, which the mapping does not need to
    // stay alive. Consumes fd on every path.
    static util::expected<shared_mapping, std::string> map(std::string name,
                                                           int fd) {
        void* p = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, 0);
        std::string err{strerror(errno)};
        close(fd);
        if (p == MAP_FAILED) {
            return util::unexpected(
                fmt::format("unable to map shm {}: {}", name, err));
        }
        return shared_mapping{std::move(name), static_cast<T*>(p)};
    }

    void release() {
        if (data_ != nullptr) {
            munmap(data_, sizeof(T));
            data_ = nullptr;
        }
    }

    std::string name_;
    T* data_ = nullptr;
};

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
        Tf_(impl_->shared->procs_remaining == 0,
            "barrier: expected 0 peers left but found {}",
            impl_->shared->procs_remaining);
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
