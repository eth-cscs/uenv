// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <argparse/argparse.h>

#include "uenv.h"

namespace uenv {

// the `uenv image inspect` command, also available as `uenv inspect`
argparse::command image_inspect_command(const global_settings& settings);

} // namespace uenv
