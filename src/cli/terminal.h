#pragma once

// this is in src/cli and not in src/util because only cli code should
// be printing messages to the terminal - core library should only ever
// print messages via spdlog.

#include <fmt/core.h>

#include <util/color.h>

namespace term {

template <typename... T> void warn(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "{}: {}\n", ::color::yellow("warning"),
               fmt::vformat(fmt, fmt::make_format_args(args...)));
}

template <typename... T> void error(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "{}: {}\n", ::color::red("error"),
               fmt::vformat(fmt, fmt::make_format_args(args...)));
}

template <typename... T> void msg(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stdout, "{}\n",
               fmt::vformat(fmt, fmt::make_format_args(args...)));
}

} // namespace term
