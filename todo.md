+ `SLURM_UENV` and `SLURM_UENV_VIEW` -> `SBATCH_UENV` etc
+ add `SBATCH_UENV_REPO` flag
+ `SLURM_UENV`, `SLURM_UENV_VIEW` and `SLURM_UENV_REPO` are set by the plugin


## requirements

A uenv is mounted in the remote context if one of the following:

1. srun/sbatch provided an explicit `--uenv` argument
    - set views
    - set and unset uenv environment variables
    - set and unset slurm environment variables
2. srun/sbatch had `SBATCH_UENV` set in the environment
    - same as 1.
3. srun had a uenv mounted in the calling environment
    - do not set views

other contexts

1. srun/sbatch called with no uenv args, env vars or existing mounts
    - noop
1. sbatch called with no uenv args/env vars _and_ `UENV_MOUNT_LIST` is set (uenv is mounted)
    - warning
    - unset `UENV_*` environment variables

The following must be performed by the slurm plugin iff a uenv is to be mounted in the remote context

- environment variables for views are set
    - skip if already set
- `SLURM_UENV`, `SLURM_UENV_VIEW`, `SLURM_UENV_REPO` are set
- `SBATCH_UENV`, `SBATCH_UENV_VIEW`, `SBATCH_UENV_REPO` are unset
- `UENV_VIEW` is set
- `UENV_MOUNT_LIST` is set
    - CLI:   `UENV_MOUNT_LIST` set by `squashfs_mount`
    - Slurm: `UENV_MOUNT_LIST` set in local context
- telemetry is performed

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

