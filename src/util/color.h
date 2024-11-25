#pragma once

#include <fmt/color.h>

#define MAKE_COLOR(color)                                                      \
    static auto color() {                                                      \
        return fmt::emphasis::bold | fg(fmt::terminal_color::color);           \
    }                                                                          \
    template <typename S> constexpr auto color(const S& s) {                   \
        return use_color() ? fmt::format(color(), "{}", s) : std::string(s);   \
    }

namespace color {

void default_color();
void set_color(bool v);
bool use_color();

MAKE_COLOR(black)
MAKE_COLOR(red)
MAKE_COLOR(green)
MAKE_COLOR(yellow)
MAKE_COLOR(blue)
MAKE_COLOR(magenta)
MAKE_COLOR(cyan)
MAKE_COLOR(white)
MAKE_COLOR(bright_black)
MAKE_COLOR(bright_red)
MAKE_COLOR(bright_green)
MAKE_COLOR(bright_yellow)
MAKE_COLOR(bright_blue)
MAKE_COLOR(bright_magenta)
MAKE_COLOR(bright_cyan)
MAKE_COLOR(bright_white)

} // namespace color
