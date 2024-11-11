#pragma once

#include <type_traits>
#include <utility>

namespace util {

template <class T>
concept nothrow_move_constructible =
    std::is_nothrow_move_constructible<T>::value;

// Convenience class for RAII control of resources.
template <nothrow_move_constructible F> class defer_wrap {
    F on_exit;
    bool trigger = true;

  public:
    template <typename F2, typename = std::enable_if_t<
                               std::is_nothrow_constructible<F, F2>::value>>
    explicit defer_wrap(F2&& f) noexcept : on_exit(std::forward<F2>(f)) {
    }

    defer_wrap(defer_wrap&& other) noexcept
        : on_exit(std::move(other.on_exit)) {
        other.release();
    }

    void release() noexcept {
        trigger = false;
    }

    ~defer_wrap() noexcept(noexcept(on_exit())) {
        if (trigger)
            on_exit();
    }
};

template <typename F> auto defer(F&& f) {
    return defer_wrap<std::decay_t<F>>(std::forward<F>(f));
}

} // namespace util
