// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <argparse/argparse.h>

#include "uenv.h"

namespace uenv {

// the `uenv image push` command
argparse::command image_push_command(const global_settings& settings);

} // namespace uenv
