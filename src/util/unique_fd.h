#pragma once

#include <utility>

#include <unistd.h>

namespace util {

// A move-only owner of a file descriptor, closed on destruction.
//
// Used where a descriptor has to outlive the call that opened it and travel
// through error-handling code: uenv::opened_image carries the descriptor of an
// image that was opened with the calling user's credentials from the point of
// the open all the way to LOOP_CONFIGURE, across several error exits. A
// util::defer at the open site cannot express that, because ownership has to
// move with the value.
class unique_fd {
  public:
    unique_fd() = default;

    explicit unique_fd(int fd) noexcept : fd_(fd) {
    }

    unique_fd(unique_fd&& other) noexcept : fd_(other.release()) {
    }

    unique_fd& operator=(unique_fd&& other) noexcept {
        // guard against self-move: reset(release()) would close the descriptor
        // and then store it back.
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

    ~unique_fd() {
        reset();
    }

    int get() const noexcept {
        return fd_;
    }

    // give up ownership without closing.
    int release() noexcept {
        return std::exchange(fd_, -1);
    }

    // close the descriptor currently held, if any, and take ownership of `fd`.
    // close() is not retried on EINTR: on Linux the descriptor is always
    // released, so a retry would close an unrelated descriptor.
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

    explicit operator bool() const noexcept {
        return fd_ >= 0;
    }

  private:
    int fd_ = -1;
};

} // namespace util
