#include <unistd.h>
#include <stdio.h>

#include <fmt/format.h>

#include "color.h"

namespace color {

namespace impl {
bool use = true;
}

void default_color() {
    // check whether NO_COLOR has been set
    if (std::getenv("NO_COLOR")) {
        set_color(false);
        return;
    }

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
