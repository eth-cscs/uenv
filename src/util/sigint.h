#pragma once

#include <atomic>
#include <csignal>
#include <type_traits>
#include <utility>

namespace util {

template <class T>
concept nothrow_move_constructible =
    std::is_nothrow_move_constructible<T>::value;

// Convenience class for RAII control of resources.
template <nothrow_move_constructible F> class sigint_wrap {
    F on_interrupt;
    inline static std::atomic<bool> triggered{false};

  public:
    template <typename F2, typename = std::enable_if_t<
                               std::is_nothrow_constructible<F, F2>::value>>

    explicit sigint_wrap(F2&& f) noexcept : on_interrupt(std::forward<F2>(f)) {
        std::signal(SIGINT, &sigint_wrap::handler);
    }

    static void handler(int signal) {
        if (signal == SIGINT) {
            triggered = false;
        }
    }

    void release() noexcept {
        triggered = false;
    }

    ~sigint_wrap() noexcept(noexcept(on_interrupt())) {
        if (triggered) {
            on_interrupt();
        }
    }
};

template <typename F> auto signit_handle(F&& f) {
    return sigint_wrap<std::decay_t<F>>(std::forward<F>(f));
}

} // namespace util
