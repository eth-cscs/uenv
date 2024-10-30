// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include "add_remove.h"
#include "ls.h"
#include "uenv.h"

namespace uenv {

void image_help();

struct image_args {
    image_ls_args ls_args;
    image_add_args add_args;
    image_remove_args remove_args;
    void add_cli(CLI::App&, global_settings& settings);
};

} // namespace uenv
