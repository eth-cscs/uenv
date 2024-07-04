// vim: ts=4 sts=4 sw=4 et

#include <vector>

#include <fmt/core.h>
// #include <tinyopt/tinyopt.h>

#include "start.h"
#include "uenv.h"

namespace uenv {

void start_help() {
    fmt::println("start help");
}

void echo(std::string msg) {
    fmt::println("image name: {}", msg);
}

/*
std::vector<to::option> start_options::cli_options(int &mode, std::string &name) {
    using namespace to::literals;
    return {{to::set(mode, mode_start), to::flag, "start", to::then(mode_start)},
            {name, to::when(mode_start)},
            {to::action(start_help), to::flag, to::exit, "-h", "--help", to::when(mode_start)}};
}
*/

void start(const start_options &options, const global_options &settings) {
    fmt::println("running start with options {}", options);
}

} // namespace uenv
