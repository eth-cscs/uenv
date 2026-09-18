// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <argparse/argparse.h>

#include "uenv.h"

namespace uenv {

// the `uenv run` command
argparse::command run_command(const global_settings& settings);

} // namespace uenv
