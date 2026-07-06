#pragma once

#include <optional>
#include <string>

extern "C" {
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
}

#include <uenv/env.h>
#include <uenv/telemetry.h>

#include "plugin.h"

namespace uenv::slurm {

// wrapper spank_getenv : for use in the remote context
std::optional<std::string> getenv_wrapper(spank_t sp, const char* var);

void patch_slurm_environment(const uenv::env&, const envvars::state&,
                             const arg_pack&);

bool in_slurm_uenv_session(const envvars::state& env);

} // namespace uenv::slurm
