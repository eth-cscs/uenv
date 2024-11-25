#pragma once

#include <atomic>
#include <csignal>
#include <type_traits>
#include <utility>

namespace util {

template <class T>
concept nothrow_move_constructible =
    std::is_nothrow_move_constructible<T>::value;

// RAII interrupt signal handler
template <nothrow_move_constructible F> class sigint_wrap {
    F on_interrupt;
    inline static std::atomic<bool> triggered{false};
    struct sigaction old_handler;

  public:
    template <typename F2, typename = std::enable_if_t<
                               std::is_nothrow_constructible<F, F2>::value>>

    explicit sigint_wrap(F2&& f) noexcept : on_interrupt(std::forward<F2>(f)) {
        // std::signal(SIGINT, &sigint_wrap::handler);
        struct sigaction h;
        h.sa_handler = &sigint_wrap::handler;
        sigemptyset(&h.sa_mask);
        h.sa_flags = 0;

        // set the new handler, keeping a copy of the old handler
        sigaction(SIGINT, &h, &old_handler);
    }

    static void handler(int signal) {
        if (signal == SIGINT) {
            triggered = true;
        }
    }

    void release() noexcept {
        triggered = false;
    }

    ~sigint_wrap() noexcept(noexcept(on_interrupt())) {
        // reset the old handler before proceeding
        sigaction(SIGINT, &old_handler, nullptr);

        // make the callback if the handler was triggered
        if (triggered) {
            on_interrupt();
        }

        // triggered is a static variable, so it needs to be reset for the next
        // time that a guard is used.
        triggered = false;
    }
};

template <typename F> auto sigint_handle(F&& f) {
    return sigint_wrap<std::decay_t<F>>(std::forward<F>(f));
}

} // namespace util
