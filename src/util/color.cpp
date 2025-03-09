#include <stdio.h>
#include <unistd.h>

#include <fmt/format.h>

#include <util/color.h>
#include <util/environment.h>

namespace color {

namespace impl {
bool use = true;
}

bool default_color(const environment::variables& calling_env) {
    // disable color if NO_COLOR env. variable is set
    if (calling_env.get("NO_COLOR")) {
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
