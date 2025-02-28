#include <stdio.h>
#include <unistd.h>

#include <fmt/format.h>

#include "color.h"

namespace color {

namespace impl {
bool use = true;
}

bool default_color() {
    // disable color if NO_COLOR env. variable is set
    if (std::getenv("NO_COLOR")) {
        return false;
    }

    // disable color if stdout is not a terminal
    if (!isatty(fileno(stdout))) {
        return false;
    }

    // otherwise use color
    return true;
}

void set_color(bool v) {
    impl::use = v;
}

bool use_color() {
    return impl::use;
}

} // namespace color
