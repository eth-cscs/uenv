// vim: ts=4 sts=4 sw=4 et
#pragma once

#include "add_remove.h"
#include "copy.h"
#include "delete.h"
#include "find.h"
#include "inspect.h"
#include "ls.h"
#include "pull.h"
#include "push.h"
#include "uenv.h"

namespace uenv {

void image_help();

struct image_args {
    image_add_args add_args;
    image_copy_args copy_args;
    image_delete_args delete_args;
    image_find_args find_args;
    image_inspect_args inspect_args;
    image_ls_args ls_args;
    image_pull_args pull_args;
    image_push_args push_args;
    image_rm_args remove_args;
    void add_cli(CLI::App&, global_settings& settings);
};

} // namespace uenv
