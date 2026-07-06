#include "environ.h"

#include <optional>
#include <string>

extern "C" {
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
}

#include <uenv/env.h>
#include <uenv/parse.h>
#include <uenv/telemetry.h>

#include "environ.h"
#include "plugin.h"

namespace uenv::slurm {

// wrapper spank_getenv : for use in the remote context
std::optional<std::string> getenv_wrapper(spank_t sp, const char* var) {
    const int len = 1024;
    char buf[len];

    const auto ret = spank_getenv(sp, var, buf, len);

    if (ret == ESPANK_ENV_NOEXIST) {
        return std::nullopt;
    }

    if (ret == ESPANK_SUCCESS) {
        return std::string{buf};
    }

    slurm_spank_log("getenv failed");
    throw ret;
}

bool in_slurm_uenv_session(const envvars::state& env) {
    return uenv::in_uenv_session(env) && env.get("SLURM_UENV");
}

// Update the calling environment to apply the environment variable updates.
// note: making this into a nice clean function that returns state that can be
// used by the calling slurm plugin was too hard - because slurm requires that
// the use of setenv/getenv/unsetenv or spank_getenv/spank_setenv/spank_unsetenv
void patch_slurm_environment(const uenv::env& environment,
                             envvars::state const& base, const arg_pack& args) {
    // create the full environment with all patches applied
    envvars::state full_env =
        generate_environment(environment, base, std::nullopt);

    // get the patch that was applied
    auto patch = environment.patch();

    // iterate over all variables in the patch:
    // - if the variable was unset: unset it in the calling environment
    // - if the variable was set: apply the new value to the calling environment
    for (auto& [name, _] : patch.scalars()) {
        if (auto value = full_env.get(name)) {
            setenv(name.c_str(), value->c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }
    for (auto& [name, var] : patch.prefix_paths()) {
        if (auto value = full_env.get(name)) {
            setenv(name.c_str(), value->c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }

    // unset the SBATCH_UENV* environment variables, so that they can't
    // affect calls to srun and sbatch inside the called script/jobstep
    for (const auto name :
         {"SBATCH_UENV", "SBATCH_UENV_VIEW", "SBATCH_UENV_REPO"}) {
        if (full_env.get(name)) {
            ::unsetenv(name);
        }
    }

    std::string view_v =
        full_env.get("UENV_VIEW").value_or(args.view_description.value_or(""));
    std::string repo_v =
        full_env.get("UENV_REPO").value_or(args.repo_description.value_or(""));
    std::string uenv_v =
        full_env.get("UENV_LABEL").value_or(args.uenv_description.value_or(""));
    ::setenv("SLURM_UENV", uenv_v.c_str(), 1);
    ::setenv("SLURM_UENV_VIEW", view_v.c_str(), 1);
    ::setenv("SLURM_UENV_REPO", repo_v.c_str(), 1);
}

} // namespace uenv::slurm
