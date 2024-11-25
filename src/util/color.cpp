#include <stdio.h>
#include <unistd.h>

#include <fmt/format.h>

#include "color.h"

namespace color {

namespace impl {
bool use = true;
}

void default_color() {
    // enable color by default
    set_color(true);

    // disable color if NO_COLOR env. variable is set
    if (std::getenv("NO_COLOR")) {
        set_color(false);
    }

    // disable color if stdout is not a terminal
    if (!isatty(fileno(stdout))) {
        set_color(false);
    }
}

void set_color(bool v) {
    impl::use = v;
}

bool use_color() {
    return impl::use;
}

} // namespace color
