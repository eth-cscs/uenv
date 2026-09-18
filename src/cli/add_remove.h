// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <argparse/argparse.h>

#include "uenv.h"

namespace uenv {

// the `uenv image add` command
argparse::command image_add_command(const global_settings& settings);

// the `uenv image rm` command
argparse::command image_rm_command(const global_settings& settings);

} // namespace uenv
