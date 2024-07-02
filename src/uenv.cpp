// vim: ts=4 sts=4 sw=4 et

#include <fmt/core.h>
#include <tinyopt/tinyopt.h>

#include "start.h"
#include "uenv.h"

// some global variables
namespace uenv {
int mode = 0;
bool verbose = false;
} // namespace uenv

void help() {
    fmt::println("usage");
}

int main(int argc, char **argv) {
    uenv::start_info start;

    std::vector<to::option> cli_options = {{to::set(uenv::verbose, true), to::flag, "-v", "--verbose"},
                                           {to::action(help), to::flag, to::exit, "-h", "--help"}};

    const auto start_opts = start.options();
    cli_options.insert(cli_options.end(), start_opts.begin(), start_opts.end());

    to::run(cli_options, argc, argv + 1);

    fmt::println("verbose {}", uenv::verbose);
}
