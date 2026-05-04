+ `SLURM_UENV` and `SLURM_UENV_VIEW` -> `SBATCH_UENV` etc
+ add `SBATCH_UENV_REPO` flag
+ `SLURM_UENV`, `SLURM_UENV_VIEW` and `SLURM_UENV_REPO` are set by the plugin


The current design is faulty, because it takes two paths:

**PATH 1**

if `UENV_MOUNT_LIST` is set and no uenv args are passed it exits early with success, and relies on the mount being performed using this information in the remote context.

**PATH 2**

if `--uenv` is passed the environment is concretised

- environment is updated
- telemetry is performed
- `SLURM_UENV_*` are set
- `SBATCH_UENV_*` are unset
- `UENV_MOUNT_LIST*` is set

**the problem**

This means that in path 1

- the `SLURM_UENV_` variables may not be set
- the `SBATCH_UENV_` variables may not be unsset
- telemetry is not performed

We should be able to test the first two

The plugin needs to be restructured into a sequence of stages that are always performed.
