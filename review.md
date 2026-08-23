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
operational event, no code defect needed), `lock` (a POSIX named semaphore at
`/dev/shm/sem.uenv-run_sem-{tag}`, initial value 1) is never posted back. POSIX
semaphores have no owner-death recovery, so the value just stays at 0. Each
subsequent `--join` attempt against that tag doesn't hang forever — `wait()`
uses `sem_timedwait()` with a fixed 30s deadline (`barrier_timeout` in
`proc_barrier.cpp`) — but it fails the same way every time thereafter, since
nothing ever posts the semaphore. The tag is permanently wedged until an
operator manually unlinks the semaphore (and the paired `/uenv-run_shm-{tag}`
mapping) from `/dev/shm`, or the node reboots (`/dev/shm` is typically tmpfs).

Note: this scenario was originally written against finding 1's *unfixed*
literal tag, where a wedge was node-wide and cross-user. Now that finding 1
scopes the tag to `SLURM_JOBID`-`SLURM_STEPID`, a wedge from this finding is
scoped to that job step instead — still a permanent, unrecoverable-without-an-
operator failure for any repeat or racing `--join` call sharing that job/step,
but no longer node-wide. At CSCS specifically, the aggressive Slurm prologue
that clears `/dev/shm` and kills stray daemons before each job bounds the
practical impact further: since step ids are never reused within a job, the
worst case degrades to "the other local tasks in that one step each stall for
the fixed 30s timeout, then fail with a clear error" rather than an
operator-page-worthy leak — a real but bounded, self-healing job failure. That
is a site-specific mitigation, not something the code can rely on in general.

**Fixed:** the barrier's `lock` was replaced with a `robust_mutex`
(`src/util/robust_mutex.h`) embedded in the barrier's shared-memory segment —
`PTHREAD_MUTEX_ROBUST` + `PTHREAD_PROCESS_SHARED`, the purpose-built Linux
primitive for exactly this problem: the kernel walks a dying process's robust
mutex list on *any* exit path, SIGKILL and OOM-kill included, so the next
`pthread_mutex_lock()` gets `EOWNERDEAD` immediately instead of a 30s timeout.
The peer that observes `EOWNERDEAD` deliberately does not call
`pthread_mutex_consistent()` before unlocking — per POSIX, that leaves the
mutex permanently `ENOTRECOVERABLE` for every other current or future peer, so
the whole barrier fails cleanly and uniformly rather than one peer silently
resuming on state nobody finished writing. No attempt is made to resume the
dead leader's setup or elect a replacement — the failure is deliberately
terminal (see the "clean up and get out" discussion this was designed
against). The original `lock` semaphore is kept, renamed `bootstrap`, but its
role is now narrowed to the brief `create_exclusive()`/`open_existing()`
race, no longer held across the mount; `end()`'s `procs_remaining` decrement
was moved onto the same robust mutex for the same reason.

One correctness trap surfaced by testing this: the failure-path helper
(`fail_after_owner_death()`, formerly named `dead_leader_cleanup()`)
originally unlinked the shm/semaphore objects eagerly on `EOWNERDEAD`. A
multi-follower unit test caught that this races a peer that has not yet
reached its own `create_exclusive()`/`open_existing()` call — freeing the shm
name lets that late peer's `create_exclusive()` (`O_EXCL`) succeed, making it
a bogus second leader for a barrier that had already failed.

**This finding is only half fixed, deliberately.** The fix eliminates the
part that was actually damaging: instead of every future `--join` attempt on
a wedged tag hanging for the full 30s timeout and then failing, forever, the
first attempt after the leader's death gets an immediate, clear error, and
every attempt after that fails the same clean way (the mutex is permanently
`ENOTRECOVERABLE`, so it can never silently look fine again). What it does
*not* do is reclaim the `/dev/shm` objects — `fail_after_owner_death()`
unlocks the mutex and returns an error, nothing else; the shm segment and
both semaphores are abandoned. A "last of `nprocs` peers to arrive can safely
unlink" scheme was considered and rejected: it assumes every peer eventually
calls `create()`, but the same OOM event that killed the leader could kill a
second task before it gets there, in which case the count never completes
and nothing is ever safely unlinked — trading one wedge for a subtler one.
The leak is therefore left as-is — same shape as, and now the same accepted
trade-off as, findings 3/4 below — rather than solved here. It is inert (a
tag is never reused within a job, so nothing ever looks the leaked names up
again) and, per the note above, further bounded at CSCS by the per-job
prologue.

`robust_mutex::lock()` also gained a `recover` parameter (default `false`):
when a caller *can* actually repair the state a mutex guards, passing `true`
calls `pthread_mutex_consistent()` on `EOWNERDEAD` so the mutex stays usable
normally, rather than proc_barrier's always-fail-clean policy. proc_barrier
itself doesn't use it (its two call sites rely on the default), but the
wrapper is meant to be reusable beyond this one fail-clean use case.

Covered by `test/unit/proc_barrier.cpp`: a leader SIGKILLed while holding the
lock is reported well under the old 30s timeout (not silently, not by
hanging); a second attempt on the same tag also fails cleanly rather than
racing to become a new leader; and every peer racing to join after the leader
dies fails, not just the one that observed `EOWNERDEAD`.

### 3. `proc_barrier::create()` leaves its bootstrap semaphore held on error paths — `src/util/proc_barrier.cpp:104` — closed, not a bug

`proc_barrier::create()` acquires the `bootstrap` semaphore (renamed from
`lock` by finding #2's fix), then several error paths (leader's
`create_exclusive`/`setup.init`/`setup.lock` failing, a follower's
`open_existing` failing) return without posting it back, and no
`proc_barrier` object yet exists whose destructor could.

**Re-evaluated:** this was originally written as a plain leak to fix, but the
requirement is that *any* peer failing during barrier creation must fail the
whole rendezvous -- proceeding with fewer than `nprocs` peers is not
acceptable. Leaving `bootstrap` held at 0 is exactly what enforces that:
every other peer, whenever it arrives (even long after the failure, even
after a purely transient cause like `ENOSPC`/`EMFILE`/`ENOMEM` has cleared),
blocks on its own `wait()` and times out rather than racing past this point
to a rendezvous that no longer includes the failed peer. Posting `bootstrap`
back on these paths -- the fix originally proposed here -- was rejected
specifically because it removes that guarantee: a peer arriving after the
failure would retry `create_exclusive()` fresh and could succeed alongside
the surviving peers while the failed one has already exited, silently
quorating on `< nprocs`. It would also, for the `setup.init`/`setup.lock`
failure paths specifically, let a follower reach `open_existing()` on a
`shared_state` whose `setup` mutex was never `pthread_mutex_init()`-ed --
undefined behaviour, though in practice a locked-then-unlocked no-op on
glibc/Linux since a zero-filled `pthread_mutex_t` reads as a valid unlocked
default mutex, silently reporting a successful join on uninitialized state.

**Left as-is, deliberately:** same accepted trade-off as finding #2's
residual leak -- a `barrier_timeout`-bounded (30s), clean, hard failure for
every peer racing on that tag, at the cost of abandoned `/dev/shm` objects
for that tag's lifetime (bounded by the tag being job/step-scoped since
finding #1, and by CSCS's per-job prologue clearing `/dev/shm`). Documented
at the point of `bootstrap->wait()` in `create()` so it isn't mistaken for an
oversight and "fixed" into the bug this reasoning avoids.

### 4. `shared_mapping::create_exclusive()` can leave a SIGBUS trap behind — `src/util/shared_mapping.h:66` — not applicable via `proc_barrier`

`create_exclusive()`'s `ftruncate()` failure path (and the subsequent
`map()`/mmap failure path) close the fd and return an error without
`shm_unlink()`-ing the shared-memory object that was just exclusively created.

**Failure scenario:** `ftruncate()` failing (ENOSPC/ENOMEM on a constrained
`/dev/shm` tmpfs) leaves a permanent 0-byte-but-named `/uenv-run_shm-{tag}`.
A later `proc_barrier::create()` for that tag that reached `open_existing()`
would get `EEXIST`, treat itself as a follower, and its own
`open_existing()` -> `mmap(sizeof(shared_state))` would succeed at the mmap
call but SIGBUS the first time it touched `shared->procs_remaining` or
`leader_pid`, since the backing object is smaller than the mapped length.

**Re-evaluated:** unreachable in practice through `proc_barrier`, given
finding #3's resolution. `shared_mapping::create_exclusive()` is only ever
called from `proc_barrier::create()`'s leader branch, strictly after this
peer's own `bootstrap->wait()` has succeeded; since finding #3 leaves
`bootstrap` permanently held on this exact failure, no peer -- present or
future, for this tag -- can ever reach `open_existing()` on the broken
object. Left unfixed here as a result; still a real latent bug in
`shared_mapping.h` as a generic reusable utility (a future caller unrelated
to `proc_barrier` could hit it), so worth an independent hardening pass if
`shared_mapping` grows other callers, but not bundled into this change.

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

**Fixed:** the leader now publishes its start time (`process_start_time()`,
field 22 of `/proc/pid/stat`) alongside its pid in `proc_barrier::ready()`,
stored in the barrier's shared memory next to `leader_pid` and exposed as
`leader_start_time()`. `namespace_join()` re-reads the target pid's start
time after `open()`-ing its namespace file but before calling `setns()`, and
refuses to join on a mismatch — `open()` pins the namespace once it succeeds,
so a match at that point guarantees the fd refers to the process the caller
actually meant, not one that reused its pid. `ready()` now fails outright if
the published pid's start time can't be read, so a pid can only be published
while it's still alive. Covered by `test/unit/setns.cpp` (start-time
stability for a live process, failure once a process has exited, and
`namespace_join()` refusing a deliberately mismatched start time without
needing namespace privileges to test) and two new cases in
`test/unit/proc_barrier.cpp` (`leader_start_time()` observed by followers
matches the published pid's actual start time; `ready()` fails for a pid that
no longer exists).

This narrows but does not eliminate the race to the theoretical limit: two
processes starting within the same clock tick (`/proc/pid/stat`'s
`starttime` has `USER_HZ` granularity, often 10ms) could in principle share a
value, so pairing the pid with that exact tick's worth of luck could still
collide. Considered and rejected as the primary fix: passing real fds from
leader to followers over a `SCM_RIGHTS` control socket, which would close the
window entirely rather than narrow it — but this IPC model has no such
channel today (pure shared memory + named semaphores), so it would be a much
larger change for a residual risk this small.

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

**Fixed**: replaced `exit(0)` with `_exit(0)`

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
