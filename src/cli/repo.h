// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <argparse/argparse.h>

#include "uenv.h"

namespace uenv {

// the `uenv repo` command, and its subcommands
argparse::command repo_command(const global_settings& settings);

} // namespace uenv
