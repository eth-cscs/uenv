// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <argparse/argparse.h>

#include "uenv.h"

namespace uenv {

// the `uenv config` command
argparse::command config_command(const global_settings& settings);

} // namespace uenv
