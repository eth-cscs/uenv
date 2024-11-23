#pragma once

#include <fmt/core.h>

namespace term {

template <typename... T>
void warn(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "warning: {}\n", fmt::vformat(fmt, fmt::make_format_args(args...)));
}

template <typename... T>
void error(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "error: {}\n", fmt::vformat(fmt, fmt::make_format_args(args...)));
}

template <typename... T>
void msg(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stdout, fmt, args...);
}

} // namespace term
