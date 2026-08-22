#pragma once

#include <pthread.h>

#include <cerrno>
#include <cstring>
#include <string>

#include <fmt/core.h>

#include <util/expected.h>

namespace util {

// A pthread mutex for use as a field inside a util::shared_mapping<T>:
// PTHREAD_PROCESS_SHARED so any peer with the mapping can lock/unlock it,
// PTHREAD_MUTEX_ROBUST so a peer that dies -- SIGKILL, OOM-kill, anything --
// while holding it is reported to the next lock() instead of wedging it
// forever. init() must be called exactly once, by whichever peer creates the
// shared memory this lives in, before any other peer can see it.
class robust_mutex {
  public:
    enum class owner_state { ok, previous_owner_died };

    util::expected<void, std::string> init() {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        int rc = pthread_mutex_init(&mutex_, &attr);
        pthread_mutexattr_destroy(&attr);
        if (rc != 0) {
            return util::unexpected(
                fmt::format("pthread_mutex_init failed: {}", strerror(rc)));
        }
        return {};
    }

    // `recover`: on previous_owner_died, whether to call
    // pthread_mutex_consistent() before returning so the mutex stays usable
    // normally. Defaults to false: unlocking without it leaves the mutex
    // permanently ENOTRECOVERABLE for every other peer -- what proc_barrier
    // wants, so the whole barrier fails rather than one peer resuming on
    // state nobody finished writing. A caller that can actually repair the
    // state it guards should pass true instead.
    util::expected<owner_state, std::string> lock(bool recover = false) {
        int rc = pthread_mutex_lock(&mutex_);
        if (rc == 0) {
            return owner_state::ok;
        }
        if (rc == EOWNERDEAD) {
            if (recover) {
                pthread_mutex_consistent(&mutex_);
            }
            return owner_state::previous_owner_died;
        }
        return util::unexpected(
            fmt::format("pthread_mutex_lock failed: {}", strerror(rc)));
    }

    util::expected<void, std::string> unlock() {
        if (int rc = pthread_mutex_unlock(&mutex_); rc != 0) {
            return util::unexpected(
                fmt::format("pthread_mutex_unlock failed: {}", strerror(rc)));
        }
        return {};
    }

  private:
    pthread_mutex_t mutex_;
};

} // namespace util
