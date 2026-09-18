// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <optional>
#include <string>

#include <argparse/argparse.h>

#include "uenv.h"

namespace uenv {

// the global options, given before any subcommand: `uenv -v --repo=... run`
struct global_args {
    int verbose = 0;
    // unset unless --color or --no-color is given
    std::optional<bool> color;
    bool version = false;
    std::optional<std::string> repo;
    std::optional<std::string> system;
};

// the uenv command line interface: the tree of every uenv command.
//
// The actions of the commands read `settings`, which must outlive the
// program; they run after main() has finished loading the configuration.
argparse::program<global_args> make_cli(const global_settings& settings);

} // namespace uenv
