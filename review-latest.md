# Code Review: `squashfuse-only` branch

Reviewed diff: `git diff @{upstream}...HEAD` (commit `05e3ba9` — "unify the rootless
mounting loop and namespacery"), touching:

- `src/slurm/plugin_fuse.cpp`
- `src/squashfs-mount/squashfs-mount-fuse.cpp`
- `src/uenv/mount_rootless.cpp`
- `src/uenv/mount_rootless.h`

NOTE: `rootless.[h,cpp]` have been renamed `mount_rootless.[h,cpp]`

Effort level: **xhigh** (10 parallel finder angles, cross-verified).

---

## Correctness findings

### 1. `src/uenv/rootless.cpp:412` — Slurm leader can end up still dumpable

**FIXED**, but not for the reason originally written up below. The real bug:
`map_effective_user`'s own `unshare(CLONE_NEWUSER | CLONE_NEWNS)` resets
DUMPABLE to 0 as a kernel side effect (the same reason
`unshare_mount_map_root` has to force it back to 1 right after its own
`unshare`), but nothing re-enabled it afterward. That's the namespace
followers actually join, so DUMPABLE was silently 0 right as `barrier->ready()`
released them to open `/proc/<leader pid>/ns/*` -- and separately,
`lock_down` only ever explicitly cleared DUMPABLE, never explicitly set it,
so a caller that started out dumpable (SLURM's actual policy notwithstanding)
could end up stuck non-dumpable regardless of `dumps_allowed`. Fixed by
forcing DUMPABLE back to 1 immediately after `map_effective_user`'s `unshare`,
and making `lock_down` set it explicitly in both directions
(`prctl(PR_SET_DUMPABLE, dumps_allowed ? 1 : 0)`) instead of only clearing it.

Verified with a manual two-peer `squashfs-mount --join` smoke test (real
`unshare`/`setns`, not mocked); both peers still exec successfully. The
original write-up below is left for context but its "typically true, so
`lock_down` skips the disable" reasoning doesn't hold: SLURM disables
DUMPABLE before invoking plugin callbacks, so `dumps_allowed` was typically
`false` for the Slurm leader, not `true`.

The refactor turns the Slurm plugin's previously-unconditional "always end
non-dumpable" hardening into a conditional restore-original-state policy, so
the leader task can now finish the mount dance still dumpable.

Old `plugin_fuse.cpp` always called `prctl(PR_SET_DUMPABLE, 0)` after
`wait_peers()` regardless of starting state (the deleted comment labelled this
"slurm policy"). The new `lock_down(dumps_allowed)` (`rootless.cpp:173-184`),
fed by `dumps_allowed = prctl(PR_GET_DUMPABLE) == 1` captured at line 412
before any `unshare`, only disables `DUMPABLE` `if (!dumps_allowed)`. A grep
of the whole tree shows only two `PR_SET_DUMPABLE` call sites left:
`unshare_mount_map_root` forces it to 1 (`rootless.cpp:68`), and `lock_down`
conditionally sets it back to 0. Since slurmstepd task processes are ordinary
(non-setuid) processes and default to dumpable=1, `dumps_allowed` is
typically true, so `lock_down` (called at line 455 for the multi-task leader,
and via `exit_sandbox` at line 419 for the new `ntasks==1` fast path that
didn't exist in the old Slurm code) now skips the disable, leaving the
mount-owning process ptraceable / `/proc/<pid>/ns` readable by any other
process sharing the same real uid indefinitely — a security-hardening
regression, not a deliberate documented tradeoff.

### 2. `src/uenv/rootless.cpp:473` — barrier teardown failure silently swallowed

**FIXED**: `barrier->end()` failure now returns `util::unexpected` instead of
warning-and-continuing, restoring hard-error behavior. A comment notes this
could be downgraded back to a warning later if it proves noisy in practice,
but that should be a deliberate call made during a debug session, not the
default.

`barrier->end()` failure, fatal in the old Slurm plugin, is now only
`spdlog::warn`'d and treated as success — and that warning is completely
suppressed in the Slurm context, so the failure becomes invisible.

Old `plugin_fuse.cpp`:
```cpp
if (auto r = barrier->end(); !r) { slurm_error(...); return -ESPANK_ERROR; }
```
a failed barrier teardown aborted the task launch and was logged via Slurm's
own logger. New unified code (`rootless.cpp:473-475`):
```cpp
if (auto r = barrier->end(); !r) { spdlog::warn(...); }
```
then unconditionally `return {}`. Because `slurm_spank_task_init_sqfs_ll`
calls `uenv::init_log(spdlog::level::off)` at `plugin_fuse.cpp:44`, and
`uenv::init_log` calls `spdlog::set_level(console_log_level)`
(`src/uenv/log.cpp:19`) which gates every level including warn, this
`spdlog::warn` is dropped entirely — nothing is logged anywhere, and SPANK is
told the task init succeeded even though barrier IPC cleanup (e.g.
`sem_unlink`/`shm_unlink`) failed. The sibling `signal_done()` failure path
(`rootless.cpp:465-467`) has the same swallow-via-log-off issue; it was
already non-fatal before but was previously visible via `slurm_error`.

### 3. `src/squashfs-mount/squashfs-mount-fuse.cpp:140` — hardcoded, non-unique barrier tag

The barrier tag passed to `mount_and_join_ns` is a hardcoded literal
(`"squashfs-mount"`), so any two concurrent `--join` invocations by the same
user on the same node collide on the same barrier/IPC objects.

Pre-existing (old code used the equally-hardcoded literal `"tag"` at the
deleted `util::proc_barrier::create("tag", ntasks)` call), but the refactor
renamed rather than fixed it, and this line was rewritten by the diff. If the
same user runs `squashfs-mount --join` for two unrelated jobs/sessions
concurrently on one node, both hit the same `proc_barrier` tag; followers
from one invocation can join the wrong invocation's leader via
`namespaces_join(leader_pid, {"user","mnt"})`, or `wait_peers()`/`create()`
see the wrong peer count and hang until timeout. The Slurm plugin call site
(`plugin_fuse.cpp:63`) correctly derives a per-job-step tag
(`"{job_id}-{step_id}"`); this call site still does not.

### 4. `src/uenv/rootless.cpp:412` — `PR_GET_DUMPABLE` return value mishandled

**FIXED**: the `prctl()` result is now checked for -1 and propagated via
`util::unexpected`, and `SUID_DUMP_ROOT` (2) is treated as dumpable
(`dumps_allowed = dumpable != 0`) rather than folded into "not dumpable".

`prctl(PR_GET_DUMPABLE)` return value is not checked for -1 (error) and folds
`SUID_DUMP_ROOT` (2) into `dumps_allowed=false`, and the fallible `prctl()`
call bypasses `util::expected` unlike every other `prctl` in this file.

```cpp
const bool dumps_allowed = prctl(PR_GET_DUMPABLE) == 1;
```
treats a real `prctl()` failure (-1, e.g. under a restrictive seccomp policy)
or `SUID_DUMP_ROOT` (2, set via `/proc/sys/fs/suid_dumpable=2`) identically to
"not dumpable", silently discarding an error instead of surfacing it. This is
also a CLAUDE.md violation: the project mandates `util::expected<T,E>` for
fallible operations, and every other `prctl` call added in this same diff
(`rootless.cpp:68-71, 175-178, 180-182`) does check its return value and
propagate `util::unexpected` — this new line is the one inconsistent case.

### 5. `src/uenv/rootless.cpp:455` — NO_NEW_PRIVS applied later than before for squashfs-mount-fuse

**WON'T FIX**

For the squashfs-mount-fuse leader, `PR_SET_NO_NEW_PRIVS` is now applied only
after followers have joined (post `wait_peers`), whereas the old code applied
it before any follower was released via `barrier->ready()`.

Old squashfs-mount-fuse.cpp: `do_mounts()` called `exit_sandbox()` (which set
`NO_NEW_PRIVS`) *before* `barrier->ready()` released any follower. New
unified leader path: `unshare_and_mount` (no `NO_NEW_PRIVS`) → 
`map_effective_user` → `barrier->ready()` → `barrier->wait_peers()` →
`lock_down` (sets `NO_NEW_PRIVS`) at line 455. The leader now spends the
entire multi-task rendezvous window with new-privilege-gaining still
permitted, where before it was locked down first. Low practical impact since
`NO_NEW_PRIVS` mainly governs later `execve` calls by this same process, but
it is a real, unremarked ordering change for this call site.

### 6. `src/slurm/plugin_fuse.cpp:55` — `uid_t`/`gid_t` type slip

**FIXED**: retyped as `const gid_t gid = getgid();`.

`const uid_t gid = getgid();` should be typed `gid_t`, a copy-paste slip in
the function this diff substantially rewrote. Harmless on Linux since
`uid_t` and `gid_t` are both `unsigned int`, but it is exactly the sort of
type mismatch worth fixing while touching this function —
`squashfs-mount-fuse.cpp:55` has the correct `const gid_t gid = getgid();`
for comparison.

---

## Cleanup findings

### 7. `src/uenv/rootless.cpp:180` — duplicated NO_NEW_PRIVS helper

`lock_down`'s `PR_SET_NO_NEW_PRIVS` block duplicates
`uenv::return_to_user_and_no_new_privs` in `src/uenv/mount.cpp` (same prctl
call, same `"PR_SET_NO_NEW_PRIVS failed"` message) with no shared helper.
Maintenance cost: the "drop NO_NEW_PRIVS" idiom now exists twice across the
fuse (`rootless.cpp`) and kernel-mount (`mount.cpp:203-210`) backends.

### 8. `src/uenv/rootless.cpp:186` — `exit_sandbox` now a single-caller wrapper

`exit_sandbox` is now a 2-line wrapper (`map_effective_user` + `lock_down`)
with exactly one remaining caller (the `ntasks==1` branch of
`mount_and_join_ns` at line 419). Could be inlined now that both of its
original external callers were replaced by direct calls to
`mount_and_join_ns`.

### 9. `src/uenv/rootless.h:9` — API surface bloat

**FIXED**

5 of 7 functions declared in `rootless.h` (`unshare_mount_map_root`,
`map_effective_user`, `lock_down`, `do_sqfs_ll_mount`, `unshare_and_mount`)
plus `exit_sandbox` no longer have any caller outside `rootless.cpp` itself.
Only `mount_and_join_ns` is used externally after this refactor. Inflates
what a caller has to read, and invites misuse — e.g. calling `lock_down`
directly without having gone through the barrier dance first, silently
violating its documented precondition.

### 10. `src/uenv/rootless.h:14` — duplicated docstring

The `dumps_allowed` "restore original state" contract is documented twice,
once on `lock_down`'s declaration (lines 14-19) and again in different words
on `mount_and_join_ns`'s declaration (lines 42-45). Two docstrings must be
kept in sync if the policy ever changes.

### 11. `src/uenv/rootless.cpp:412` — generalization works by coincidence

The `dumps_allowed` generalization only produces correct-looking results for
both call sites by coincidence of each one's typical starting dumpable state,
not because it encodes either caller's actual required policy. Slurm wants an
unconditional non-dumpable end state (per the deleted "slurm policy"
comment); squashfs-mount-fuse never disabled dumpable at all in the old code.
The new single mechanism happens to reproduce something plausible only
because ordinary Slurm tasks and the non-setuid squashfs-mount binary both
start out dumpable=1 — nothing asserts or documents this assumption (see
finding 1).

### 12. `src/uenv/rootless.cpp:412` — unconditional syscall with no consumer on some paths

**FIXED as a side effect of finding 14**: the follower branch now calls
`lock_down(dumps_allowed)` too, so this is no longer a discarded result.

`dumps_allowed` is computed unconditionally at function entry but is never
consumed on the follower branch or on any early-return error path out of
`unshare_and_mount`. Every follower task in the common `ntasks>1` case pays a
`prctl(PR_GET_DUMPABLE)` syscall whose result is discarded.

### 13. `src/uenv/rootless.cpp:414` — undocumented `ntasks==1` special case

The `ntasks==1` fast path bypasses `proc_barrier` entirely with no comment
explaining why, when `proc_barrier::create` is documented to support
`nprocs==1` as a trivial leader-only case (and the old Slurm code relied on
exactly that). Worth a one-line comment given it is now shared by both call
sites.

### 14. `src/uenv/rootless.cpp:480` — followers never dropped privileges before exec

**FIXED**: only the leader ever called `lock_down` (NO_NEW_PRIVS +
DUMPABLE restore). Every follower task is a distinct process that, after
`namespaces_join` succeeds, returns to its caller and goes on to `execve()`
the real payload in that same process -- in `squashfs-mount-fuse.cpp` every
process (leader and followers alike) reaches the same `util::exec(...)` call
at the end of `main()`; in the Slurm plugin, `slurm_spank_task_init_sqfs_ll`
runs once per local task in the process slurmstepd will go on to exec the
job step in. So each of the N processes is an independent exec endpoint, but
only 1 of them was getting `NO_NEW_PRIVS` before its own exec.

This was also cross-checked against Charliecloud's `--join` implementation
(`ch-run.c`/`ch_core.c`), which this design took inspiration from: every
`--join` peer independently sets `PR_SET_NO_NEW_PRIVS` right before its own
`execve`, winner and losers alike -- there is no "only the leader needs it"
shortcut in the reference implementation either.

Fixed by calling `lock_down(dumps_allowed)` in the follower branch too,
right after `namespaces_join` succeeds (no ordering constraint applies here,
unlike the leader: nobody else needs to open a follower's own
`/proc/<pid>/ns/*` afterward). Verified with a manual 3-peer
`squashfs-mount --join` run: before the fix, only the leader's exec'd process
showed `NoNewPrivs: 1` in `/proc/self/status`; after the fix, all three do.

---

## Summary

Findings 1–2 are the most severe: a DUMPABLE-restore bug that could leave the
leader stuck non-dumpable or briefly block followers from joining its
namespaces, and a silently-swallowed IPC-teardown failure in the Slurm
plugin path, both introduced by collapsing two call-site-specific policies
into one shared function without preserving either original policy exactly.
Finding 14, raised in follow-up discussion, is arguably as severe as 1–2:
follower processes never dropped privileges before exec at all. Findings 3–6
are smaller but real correctness issues on touched lines. Findings 7–13 are
cleanup/maintenance observations, ranked below correctness per review policy;
finding 5 (NO_NEW_PRIVS timing on the leader) was reviewed and intentionally
left as-is.
