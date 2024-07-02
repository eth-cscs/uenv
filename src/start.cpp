// vim: ts=4 sts=4 sw=4 et

#include <vector>

#include <fmt/core.h>
#include <tinyopt/tinyopt.h>

#include "start.h"
#include "uenv.h"

namespace uenv {

void start_help() {
    fmt::println("start help");
}

std::vector<to::option> start_info::options() {
    using namespace to::literals;
    return {{to::set(mode, mode_start), "start", to::then(mode_start)},
            {to::set(img_argument), to::when(mode_start)},
            {to::action(start_help), to::flag, to::exit, "-h", "--help", to::when(mode_start)}};
}

} // namespace uenv
