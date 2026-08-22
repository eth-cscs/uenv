# Code Review: `squashfuse-only` branch

Reviewed diff: `git diff main...HEAD` on branch `squashfuse-only` (commit `6585ace`
at time of review), focused on security and correctness.

Effort level: **max** (10 parallel finder angles, cross-verified, gap sweep).

## What this branch does

Adds a rootless, FUSE-based squashfs mounting backend (via user/mount namespaces +
squashfuse_ll) as a build-time alternative to the existing setuid kernel-driver
backend, plus new IPC coordination primitives (`proc_barrier`, `named_semaphore`,
`shared_mapping`, `ready_fork`, `setns`) so that multiple Slurm tasks sharing a
node can rendezvous around one leader's mount.

## Where the risk concentrates

Almost every high-severity finding sits in the new coordination layer. The most
serious is a hardcoded, unscoped barrier tag in the interactive CLI path
(`squashfs-mount-fuse.cpp:185`) that lets one job's tasks join a completely
different job's mount namespace — contrasted with the Slurm-plugin path, which
correctly scopes its tag by job/step. Close behind: the barrier's `lock`
semaphore is held by the leader across the *entire* mount sequence with no
recovery if the leader is killed (an everyday HPC event via SIGKILL/OOM/job time
limits), several error paths in the new semaphore/shared-memory wrappers leak IPC
objects in a permanently-held state, and a follower joins the leader's namespace
via a bare, unverified PID (a reuse/TOCTOU race). A concrete process gap: the
four new tests meant to verify the FUSE backend's own privilege-dropping logic
all skip on that build, so that code ships with zero test coverage.

Key files: `src/uenv/mount_rootless.cpp`, `src/util/proc_barrier.cpp`/`.h`,
`src/util/shared_mapping.h`, `src/util/setns.cpp`, `src/slurm/plugin_fuse.cpp`,
`src/squashfs-mount/squashfs-mount-fuse.cpp`, `test/integration/squashfs-mount.bats`,
`src/uenv/mount_kernel.cpp`.

---

## Findings

### 1. Unscoped barrier tag lets unrelated jobs collide — `src/squashfs-mount/squashfs-mount-fuse.cpp:185`

The interactive `uenv run --join` path uses a fixed, unscoped proc_barrier tag
(`"squashfs-mount"`) for every multi-task rendezvous, instead of scoping it to a
job/step like the Slurm plugin path does.

**Failure scenario:** Two unrelated `--join` invocations land on the same
shared/oversubscribed node at the same time (two job steps, possibly the same
user's two overlapping steps). Both open identical POSIX semaphores/shm named
after the literal tag (created before any namespace unshare, so genuinely
node-global). Mode-0600 permissions mean a different uid gets a hard
permission-denied failure, but a same-uid collision succeeds silently: a
follower from invocation B calls `namespaces_join(barrier->leader_pid(), ...)`
into invocation A's leader and runs invocation B's command inside A's mounted
squashfs/user namespace, or mismatched `nprocs` drives `shared->procs_remaining`
negative in `end()`.

**Fixed:** `local_task_count()` was replaced with `uenv::local_join_context()`
(`src/uenv/join_context.h`/`.cpp`), which determines the local task count and
the barrier tag together from the same source, instead of computing the tag
separately from a fixed literal. It scopes the tag to `SLURM_JOBID`/
`SLURM_STEPID` (alongside the existing `SLURM_STEP_TASKS_PER_NODE`/
`SLURM_NODEID` used for the task count), and is a hard error if any of the
four is unset, rather than silently falling back to a shared tag. Covered by
`test/unit/join_context.cpp`, including a case asserting two different job
ids on the same node produce different tags. A common-parent-process fallback
(for non-Slurm launchers) was considered and deliberately deferred: it can
derive a collision-free tag (e.g. session id + session-leader start time),
but not a trustworthy `ntasks` — `proc_barrier::create()` needs an exact peer
count up front, and there is no race-free way to count siblings from a shared
ancestor without a barrier to synchronize the count. See the comment above
`uenv::local_join_context()` for details.

### 2. Leader holds the barrier lock across the whole mount, with no owner-death recovery — `src/uenv/mount_rootless.cpp:443`

The barrier leader holds the `lock` semaphore continuously from
`proc_barrier::create()` until `ready()` — spanning forking, mounting, and
daemonizing every squashfs image — and POSIX semaphores have no owner-death
recovery.

**Failure scenario:** If the leader process is SIGKILLed, OOM-killed, or hits a
Slurm job time limit anywhere during that multi-second window (an ordinary HPC
operational event, no code defect needed), the lock is never posted and stays
held forever. Combined with finding 1's fixed tag, this permanently wedges
`--join` for every user on that node until an operator manually clears the
semaphore from `/dev/shm` or the node reboots.

### 3. `proc_barrier::create()` leaks its lock semaphore on error paths — `src/util/proc_barrier.cpp:86`

`proc_barrier::create()` acquires the `lock` semaphore, then three separate
error paths (`create_exclusive` failing for a would-be leader at line 86-88,
`open_existing` failing for a follower at 103-105, a follower's own
`lock->post()` failing at 110-112) return without releasing it, and no
`proc_barrier` object yet exists whose destructor could.

**Failure scenario:** A transient `shm_open`/`ftruncate`/`mmap` failure (EMFILE,
ENOSPC, ENOMEM) right after this process wins the lock leaves
`/uenv-run_sem-{tag}` permanently held at 0, wedging that tag the same way as
the previous finding but via an ordinary resource hiccup instead of a job kill.

### 4. `shared_mapping::create_exclusive()` can leave a SIGBUS trap behind — `src/util/shared_mapping.h:66`

`create_exclusive()`'s `ftruncate()` failure path (and the subsequent
`map()`/mmap failure path) close the fd and return an error without
`shm_unlink()`-ing the shared-memory object that was just exclusively created.

**Failure scenario:** `ftruncate()` failing (ENOSPC/ENOMEM on a constrained
`/dev/shm` tmpfs) leaves a permanent 0-byte-but-named `/uenv-run_shm-{tag}`.
Every later `proc_barrier::create()` for that tag gets `EEXIST`, treats itself
as a follower, and its own `open_existing()` -> `mmap(sizeof(shared_state))`
succeeds at the mmap call but SIGBUSes the first time it touches
`shared->procs_remaining` or `leader_pid`, since the backing object is smaller
than the mapped length.

### 5. Namespace join by bare PID is a reuse/TOCTOU race — `src/uenv/mount_rootless.cpp:473`

A follower joins the leader's namespaces via a bare `pid_t` published through
shared memory (`namespaces_join(barrier->leader_pid(), {"user", "mnt"})`) with
no liveness or identity verification — no pidfd, no process-start-time
comparison.

**Failure scenario:** If the leader dies and the kernel recycles its pid to an
unrelated process before a slow follower calls `setns()`, the follower joins
that unrelated process's user/mount namespace instead of the intended leader's,
silently running the user's job in the wrong (or an attacker-influenced)
namespace.

### 6. `exit(0)` instead of `_exit(0)` in the forked FUSE-server child — `src/uenv/mount_rootless.cpp:385`

`do_sqfs_ll_mount`'s forked child calls plain `exit(0)` on its normal/success
shutdown path, inconsistent with `child_fail()`'s `_exit(1)` used on every
error path in the same function.

**Failure scenario:** The code's own comment on `child_fail` explains error
paths must terminate the child directly because it shares the parent's whole
call stack; the same reasoning applies to `exit()` re-running inherited atexit
handlers/static destructors (spdlog sinks, libcurl, sqlite3 statics) a second
time in this forked child on the successful-mount path, racing or duplicating
the parent's own eventual cleanup.

### 7. Unchecked `spank_get_item()` return values, plus a type mismatch — `src/slurm/plugin_fuse.cpp:60`

`spank_get_item()` return values for `S_JOB_LOCAL_TASK_COUNT`, `S_JOB_ID`, and
`S_JOB_STEPID` are never checked (unlike `plugin_kernel.cpp:60`, which checks
`S_JOB_GID`), and `int ntasks` is passed where Slurm's `spank.h` documents a
`uint32_t *` for that item.

**Failure scenario:** If any of these lookups fails, `ntasks`/`job_id`/`step_id`
silently keep their initializers (1/0/0): a failed `S_JOB_LOCAL_TASK_COUNT`
makes a genuinely multi-task node silently skip `--join`'s namespace sharing
with no error logged, and `job_id=0,step_id=0` reproduces finding 1's cross-job
tag collision for every job hitting the same failure concurrently.

### 8. `proc_barrier::end()` marks itself done before it actually is — `src/util/proc_barrier.cpp:185`

`end()` sets `impl_->ended = true` before its lock-acquire/decrement/unlink
sequence actually succeeds, so a failure partway through (e.g. the
`lock.wait()` at line 188 timing out) permanently skips the retry — including
from the destructor, which checks the same flag.

**Failure scenario:** If acquiring the lock inside `end()` times out (plausible
given finding 3's leak), `end()` returns an error but `ended` is already true;
neither a caller retry nor `~proc_barrier()` will ever re-decrement
`shared->procs_remaining`, so it may never reach 0 and the tag's IPC objects
are never unlinked by anyone.

### 9. `namespace_join()` leaks a file descriptor into the final job process — `src/util/setns.cpp:24`

`namespace_join()` opens `/proc/<pid>/ns/<ns>` into a local fd via `open()` but
never closes it on the success path or the final-retry-failure path, and
doesn't set `O_CLOEXEC`.

**Failure scenario:** Every `--join` follower leaks two fds (`ns/user`,
`ns/mnt`) per mount invocation. Lacking `O_CLOEXEC`, both survive the later
`execvp` of the user's actual job/MPI binary, which inherits stray open handles
onto another process's namespace files it never asked for.

### 10. FUSE backend's privilege-drop logic has zero test coverage — `test/integration/squashfs-mount.bats:46`

All four new privilege-drop/NoNewPrivs regression tests (lines 46, 61, 72, 86)
`skip` unless the binary is setuid, which is unconditionally false for the
entire `mount_backend=fuse` CI leg by design.

**Failure scenario:** The FUSE backend's own `map_effective_user()`/
`lock_down()` uid-drop and `NO_NEW_PRIVS` logic — exactly the security-critical
new code this PR adds — has zero automated test coverage under
`mount_backend=fuse`; a future regression there would pass CI green on that leg
while appearing covered.

### 11. FUSE mount has no explicit read-only mount option — `src/uenv/mount_rootless.cpp:304`

The FUSE mount is set up with no explicit read-only mount option (the dummy
`fuse_args` carries no `-o ro`), unlike the kernel backend's libmount call
which explicitly passes `"loop,nosuid,nodev,ro"`; the test suite confirms and
accepts this by asserting `"rw,nosuid"` for the fuse mount table entry.

**Failure scenario:** Read-only enforcement for the FUSE backend relies
entirely on squashfuse's op table never registering a `.write` callback rather
than on the mount itself — a defense-in-depth gap versus the kernel path, where
any future squashfuse op-table change (e.g. gaining a write/setxattr handler)
would silently make mounted images writable at the VFS level with no
mount-flag backstop.

### 12. Classic `PR_SET_PDEATHSIG` race — `src/uenv/mount_rootless.cpp:250`

`prctl(PR_SET_PDEATHSIG, SIGHUP)` is armed as the first statement after
`fork()` returns in the child, not before forking.

**Failure scenario:** If the parent is killed in the (very narrow) window
between `fork()` returning and this `prctl()` executing in the child, the
death signal is armed too late and never delivered; the FUSE-daemon child is
reparented to init and keeps the mount held open with nothing left able to
signal it to exit.

### 13. `wait_peers()`'s per-follower timeout can multiply the leader's wait — `src/util/proc_barrier.cpp:162`

`wait_peers()` gives each follower's `done.wait(barrier_timeout)` its own fresh
30-second deadline inside a serial loop, so the leader's worst-case wait is
`(nprocs-1)*30s` rather than the single 30s the constant's name implies.

**Failure scenario:** One hung or slow-to-signal follower makes the leader wait
out a full 30s for it before even checking the next follower, so N stuck
followers serialize into N*30s of total leader-side blocking instead of one
bounded 30s window.

### 14. Stale AI-review reports committed at the repo root — `review-latest.md:1`

`review.md` and `review-latest.md` are AI-generated code-review reports
committed directly into the repository root; `review.md` is listed in
`.gitignore` and git history shows both were deliberately "untrack"-ed before,
yet they're tracked again in this diff.

**Failure scenario:** These review artifacts duplicate each other and
reference code that has since been renamed/split (stale immediately); per
independent cross-checking, `review-latest.md` also narrates a historical
full-privilege-escalation exploit against this code — permanently baking that
writeup into the shipped repository history rather than keeping it
out-of-band.

### 15. Debug leftover leaks build path into user-facing errors — `src/uenv/mount_kernel.cpp:110`

`do_mount()`'s failure path appends `fmt::format("{}:{}", __FILE__, __LINE__)`
to the mount-failure message; confirmed absent from the pre-PR `mount.cpp`
version of this same code, so it's newly introduced by this diff.

**Failure scenario:** Any ordinary kernel-backend mount failure (bad image,
exhausted loop devices, permission issue) now leaks the build machine's
absolute source path into normal user- and Slurm-log-facing error output.
