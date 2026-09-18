// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <optional>
#include <string>

#include <argparse/argparse.h>
#include <uenv/settings.h>

#include "build.h"
#include "completion.h"
#include "config.h"
#include "image.h"
#include "repo.h"
#include "run.h"
#include "start.h"
#include "status.h"
#include "uenv.h"

namespace uenv {

// The command line interface: the tree of commands, and the variables that
// its options and positional arguments are bound to.
//
// The same tree is used to parse the command line of every invocation, and to
// complete a partial command line.
struct cli_state {
    config_base cli_config;
    bool print_version = false;
    std::optional<std::string> cli_repo;

    start_args start;
    run_args run;
    image_args image;
    repo_args repo;
    status_args stat;
    build_args build;
    completion_args completion;
    configure_args configure;

    argparse::command root;

    cli_state(global_settings& settings);
    cli_state(const cli_state&) = delete;
};

} // namespace uenv
