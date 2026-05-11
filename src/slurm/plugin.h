#pragma once

#include <optional>
#include <string>

namespace uenv::slurm {

// Marks what to do when srun/sbatch/salloc are called from an environment that
// has a uenv mounted.
//
// use      : ignore and do not use the mounted uenv
// ignore   : forward the mounted uenv (if an alternative is not explicitly
//            requested)
// disable  : treat as an error
// none     : this is the "default" setting
//            - in local (srun) context the default is use
//            - in allocator (sbatch/salloc) context the default is disable
enum class passthrough_policy { use, ignore, disable, none };

struct arg_pack {
    std::optional<std::string> uenv_description;
    std::optional<std::string> view_description;
    std::optional<std::string> repo_description;
    passthrough_policy passthrough = passthrough_policy::none;
    bool use_default_views = true;
};

} // namespace uenv::slurm
