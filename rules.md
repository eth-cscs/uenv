# uenv Slurm Plugin: Revised Behaviour

## Background

The plugin's previous design used `SLURM_UENV` and `SLURM_UENV_VIEW` as input variables — alternatives to passing `--uenv` and `--view` on the command line. This caused a re-patching bug: if those variables were present in the calling environment, the plugin would re-apply environment variable patches (PATH, LD_LIBRARY_PATH, etc.) on every `srun` invocation, even when a session was already active. A user who modified PATH after the initial allocation would silently have their modification overridden.

The deeper issue is that the naming blurred two distinct concepts: variables that are inputs (user intent) and variables that are outputs (session state). The revised design separates them clearly and tightens the rules around session inheritance.

---

## Environment Variables

| Variable | Direction | Meaning |
|---|---|---|
| `SBATCH_UENV` | Input | Default `--uenv` value for `sbatch`/`salloc`. Follows the `SBATCH_RESERVATION` convention. Unset from the downstream job environment after processing so it does not cascade to child job submissions. |
| `SBATCH_VIEW` | Input | Default `--view` value. Same lifetime as `SBATCH_UENV`. |
| `SLURM_UENV` | Output (set by plugin) | Reflects the uenv image active in the current Slurm session. Set by the plugin after establishing a session. Never read as input. Analogous to `SLURM_JOB_ID`. |
| `SLURM_VIEW` | Output (set by plugin) | Reflects the active view. Same lifetime as `SLURM_UENV`. |
| `UENV_MOUNT_LIST` | Internal | Encodes squashfs paths and mount points for the remote context. Not intended for user consumption. |

**Why unset `SBATCH_UENV` downstream?**
Unlike `SBATCH_RESERVATION`, which users reasonably expect to cascade to child job submissions, `SBATCH_UENV` should not propagate implicitly. Each job in a chain should declare its own uenv explicitly. Silent cascade leads to jobs whose behaviour depends on the submission context rather than their own script, which undermines reproducibility. Users who want a default for all submissions can set `SBATCH_UENV` in their environment; users whose jobs submit child jobs should specify `--uenv` (or `--uenv-ignore`) explicitly in those child submissions.

**Why reserve `SLURM_UENV` as output-only?**
Setting `SLURM_UENV` as a read-only reflection of the active session allows all contexts (the job script itself, nested tools, diagnostics) to inspect which uenv is live, without any risk that its presence triggers re-processing. Having a single variable serve as both input and output creates ambiguity about whether it represents intent or state.

---

## `sbatch` / `salloc` (allocator context)

### Detecting an active session

When `sbatch` or `salloc` is called, the plugin checks for signs that a uenv session is already active in the calling environment:
- `SLURM_UENV` is set → the submission is being made from inside a running Slurm uenv job.
- `UENV_MOUNT_LIST` **and** `UENV_VIEW` are both set (without `SLURM_UENV`) → the submission is being made from a CLI `uenv run` or `uenv start` session on a login node. Both variables are always set together by the CLI; checking both avoids false positives from stale or partial environment state.

### Why a hard error, not a warning?

If an active uenv session is detected and no explicit uenv is specified for the new job, the plugin **hard errors**. The reason a warning is insufficient: even if the plugin ignores `UENV_MOUNT_LIST` and does not mount the squashfs in the new job, the calling environment's PATH, LD_LIBRARY_PATH, and other variables set by the uenv view will still be inherited by the batch job (via Slurm's default `--export=ALL`). The result is a half-broken job: binaries are discovered via PATH entries that point into a squashfs mount point that does not exist on the compute node. This fails at runtime in ways that are distant from the root cause and hard to debug. A hard error at submission time is unambiguous.

### Rules

1. If an active session is detected (see above), **and** neither `--uenv` nor `--uenv-passthrough` was given → **hard error**. The error message must explain both options.

2. If an active session is detected, **and** `--uenv=<image>` was explicitly given → start fresh with the specified image. The inherited session state is fully replaced. This is the supported path for sbatch-inside-sbatch workflows where the inner job uses a different uenv.

3. If an active session is detected, **and** `--uenv-passthrough=ignore` was given → proceed with no uenv. `SBATCH_UENV` is also ignored. This is the supported escape for inner jobs that want to run clean.

4. If the calling environment is clean (no active session): resolve the uenv from `--uenv` flag > `SBATCH_UENV` env var > nothing. If a uenv was resolved: patch environment variables, set `UENV_MOUNT_LIST`, set `SLURM_UENV`/`SLURM_VIEW`, and unset `SBATCH_UENV`/`SBATCH_VIEW` from the downstream environment.

### sbatch-inside-sbatch

Some users submit child jobs conditionally from within a running job. This workflow is supported:
- If the inner `sbatch` call specifies `--uenv=<image>`, it is treated as case 2 above — no error, the inner job uses the specified image.
- If the inner `sbatch` call should run without a uenv, add `--uenv-ignore` — case 3 above.
- If the inner `sbatch` call specifies neither and a session is active — case 1, hard error. The user must be explicit.

The `#SBATCH --uenv=...` header form remains fully supported. It is the natural pattern for single-job workflows and there is no reason to discourage it.

---

## `srun` (local context)

`srun` does not re-apply environment variable patches when a session is already active. Patches are applied once, at allocation time by the allocator context. The `srun` local context is responsible only for validating and forwarding mount information to the remote context.

### Rules

1. If `--uenv=<image>` is given → full setup: resolve image, patch environment variables, set `UENV_MOUNT_LIST`, `SLURM_UENV`, `SLURM_VIEW`.

2. If `SLURM_UENV` is set (inside a running Slurm uenv job) → **mount-only mode**: do not re-patch environment variables (they were set at allocation time). Validate `UENV_MOUNT_LIST` and pass it through to the remote context. `SBATCH_UENV` is ignored.

3. If `UENV_MOUNT_LIST` and `UENV_VIEW` are both set but `SLURM_UENV` is not (inside a CLI `uenv run`/`uenv start` session) → same as case 2: validate and pass through, no environment patching. `SBATCH_UENV` is ignored.

4. If `SBATCH_UENV` is set and no active session is detected → treat as equivalent to `--uenv`. Full setup as in case 1. This supports CI/CD pipelines and other contexts where specifying flags on the command line is awkward.

5. Otherwise → no-op.

**Why does srun ignore `SBATCH_UENV` when a session is active?**
An active session (cases 2 and 3) means the environment is already configured. Allowing `SBATCH_UENV` to override it silently would reintroduce the re-patching bug under a different name. Explicit `--uenv` on the `srun` command line is the correct way to override an inherited session.

---

## Remote context (compute node)

No changes. The remote context reads `UENV_MOUNT_LIST`, validates each entry, creates a mount namespace, and mounts the squashfs images. It does not modify environment variables.

---

## Open questions

### `SBATCH_UENV` in the `srun` local context (case 4 above)

The current implementation respects `SBATCH_UENV` in `srun` even when no session is active, deviating from the strict Slurm convention that `SBATCH_*` variables only apply to `sbatch`/`salloc`.

Two options under consideration:

**Option A (current):** `srun` reads `SBATCH_UENV` when no active session is detected, treating it as equivalent to `--uenv`. Rationale: the variable is always unset by the plugin after use, so any `SBATCH_UENV` present at `srun` time inside a job script must have been deliberately re-set by the user.

**Option B:** `SBATCH_UENV` is only consumed in the allocator context (`sbatch`/`salloc`) and ignored (but still unset) in `srun`. This follows the Slurm `SBATCH_*` convention exactly and eliminates the edge case where a user with `SBATCH_UENV` in their `.bashrc` gets unexpected behaviour when calling `srun` from a CLI `uenv run` session.

The deciding factor is how CI/CD jobs are structured: if they always go through `sbatch`, option B is safe; if they call `srun` directly without a prior allocation, option B would be a breaking change. Awaiting input from the CI/CD team before deciding.
