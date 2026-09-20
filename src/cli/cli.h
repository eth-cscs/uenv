// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <optional>
#include <string>
#include <vector>

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

// The configuration of an invocation with the global options `globals`: the
// defaults, the system and user configuration files, and --repo, --system
// and --color. Used by main() and by tab completion, so that completion sees
// the configuration that the completed command will run with.
struct loaded_configuration {
    configuration config;
    // to show to the user, e.g. about values in the configuration files
    std::vector<std::string> warnings;
};
util::expected<loaded_configuration, std::string>
load_configuration(const global_args& globals, const envvars::state& env,
                   user_config_mode mode);

} // namespace uenv
