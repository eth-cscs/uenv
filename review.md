# Code review: squashfs-only branch (ultra effort)

Findings from a multi-agent `/code-review xhigh` (ultra) pass, ranked most severe first.

## 1. Privilege drop never happens (security)

**`src/squashfs-mount/squashfs-mount.cpp:274`**

The final privilege-drop is gated on `is_setuid()`, but `setup_sandbox()` already
called `setreuid(0,0)` via `uenv::unshare_and_become_root()`
(`src/uenv/mount.cpp:194`), so `geteuid()==getuid()==0` by this point and the gate
is always false, skipping `return_to_user_and_no_new_privs()`.

**Failure scenario:** Any `uenv run`/`uenv start` invocation on the shipped setuid
(non-FUSE) build mounts an image (`has_work=true`, the normal case), which sets
real=effective=saved uid to 0 via `setreuid(0,0)`; `if (is_setuid())` then
evaluates false so `return_to_user_and_no_new_privs(uid)` is never called, and
`util::exec(*commands, cenv)` execs the user's shell/command with
real=effective=saved uid=0 -- full privilege escalation to root.

## 2. Hardcoded barrier tag (correctness)

**`src/squashfs-mount/squashfs-mount.cpp:51`**

`mount_and_join_ns()` creates the process barrier with a hardcoded literal tag
`"tag"` instead of a per-job/step-unique identifier, unlike
`src/slurm/plugin_fuse.cpp` which uses `job_id-step_id`.

**Failure scenario:** `util::proc_barrier::create("tag", ntasks)` names its POSIX
objects `/uenv-run_sem-tag`, `/uenv-run_sem_ready-tag`, `/uenv-run_shm-tag` with no
job/user scoping (`sem_open` has no `O_EXCL`). Two concurrent `uenv run --join`/Slurm
invocations on the same node (different job steps or sessions) collide on the same
names, causing a follower from one job to `setns()` into the mount/user namespace
of a completely unrelated job's leader, or hang/fail on stale semaphore state.

## 3. Unguarded stoi mis-parses Slurm task-list format (correctness)

**`src/squashfs-mount/squashfs-mount.cpp:264`**

`int ntasks = std::stoi(calling_env.get("SLURM_STEP_TASKS_PER_NODE").value_or("1"))`
is unguarded: throws on malformed input and mis-parses Slurm's real compressed
multi-node format.

**Failure scenario:** No try/catch exists anywhere in the file. An empty or
non-numeric `SLURM_STEP_TASKS_PER_NODE` throws `std::invalid_argument`, aborting
the process. Separately, Slurm's real format is a run-length-encoded list like
`"5,4(x2)"` exported identically on every node; `std::stoi` only consumes the
leading digits, so on any node but the first group it silently returns the wrong
node's task count, corrupting the barrier's nprocs.

## 4. Namespace fd opened without O_CLOEXEC and never closed (resource leak)

**`src/util/setns.cpp:24`**

`namespace_join()` opens `/proc/{pid}/ns/{ns}` without `O_CLOEXEC` and never
closes the fd on any path.

**Failure scenario:** Every follower task that joins namespaces
(squashfs-mount's `mount_and_join_ns`, `plugin_fuse.cpp`'s
`slurm_spank_task_init_sqfs_ll`) leaks one fd per namespace joined. Because
`O_CLOEXEC` isn't set, these fds survive the later execvp/execvpe in
`util::exec` and are inherited by the user's job process.

## 5. exit(1) skips proc_barrier destructor, leaking IPC state (resource leak)

**`src/squashfs-mount/squashfs-mount.cpp:61`**

`error_and_exit()` calls `exit(1)` directly, which does not run destructors of
automatic-storage objects; `mount_and_join_ns()`'s `barrier`
(`std::optional<util::proc_barrier>`) is a stack local, so every
`error_and_exit()` reached while it is alive skips `~proc_barrier()` and its
`end()`-based IPC cleanup entirely.

**Failure scenario:** The leader's `do_mount()` fails after
`proc_barrier::create()` succeeded and the leader holds the `lock` semaphore.
`error_and_exit("mount failed {}", ...)` calls `exit(1)`; `~proc_barrier()`
never runs, so `lock` is never posted. Every follower blocked in `create()`'s
`lock->wait()` times out after the full 30s `barrier_timeout`, and the named
semaphore/shm objects are leaked under `/dev/shm`, poisoning every subsequent
`squashfs-mount --join` invocation on the node (compounded by the hardcoded
`"tag"` bug above).

## 6. create() error paths can leak the held lock semaphore (resource leak)

**`src/util/proc_barrier.cpp:78`**

`proc_barrier::create()` acquires the lock semaphore but several later
error-return paths (`create_exclusive` failure, `open_existing` failure,
follower's `lock->post()` failure) return without releasing it, and no
`proc_barrier` object exists yet to auto-release via destructor.

**Failure scenario:** If `shm_open` fails for a non-EEXIST reason, or a
follower's `shared_mapping::open_existing()`/`lock->post()` fails, `create()`
returns `util::unexpected` without posting the lock. `/uenv-run_sem-<tag>`
stays permanently held; any subsequent `create()` call with the same tag
blocks and times out after 30s.

## 7. end() can strand cleanup permanently on partial failure (correctness)

**`src/util/proc_barrier.cpp:205`**

`proc_barrier::end()` sets `impl_->ended=true` before cleanup; if
`lock.unlink()` succeeds but `done.unlink()` or `shared.unlink()` fails, the
function returns early and never reaches `lock.post()`, and `ended==true`
prevents any retry.

**Failure scenario:** On the last peer's cleanup, a failure in
`done.unlink()` or `shared.unlink()` after `lock.unlink()` already succeeded
permanently leaks the done semaphore name and/or shared memory segment, with
no way to retry since `end()` short-circuits from then on (including from the
destructor).

## 8. create_exclusive() leaves broken shm segment on failure (resource leak)

**`src/util/shared_mapping.h:66`**

`shared_mapping<T>::create_exclusive()` opens with `O_CREAT|O_EXCL`, but if
the subsequent `ftruncate()` or `mmap()` fails, it returns an error without
calling `shm_unlink(name)` -- the just-created, broken shm segment is left
behind under that name.

**Failure scenario:** A transient ftruncate/mmap failure (tmpfs low on
memory, ENOSPC on `/dev/shm`) leaves a zero-length shm object named e.g.
`/uenv-run_shm-<tag>` permanently in place. Because `create_exclusive` uses
`O_EXCL`, every subsequent call with the same tag gets EEXIST, becomes a
follower, and its `open_existing()`'s mmap fails against the undersized
object -- permanently breaking `proc_barrier` for that tag until an operator
manually clears `/dev/shm`.

## 9. Leader skips wait_peers(), can race ahead of followers (correctness)

**`src/squashfs-mount/squashfs-mount.cpp:66`**

The leader branch in `mount_and_join_ns()` calls `barrier->ready()` then
falls straight to `barrier->end()` with no `wait_peers()` call, unlike
`plugin_fuse.cpp`'s leader which explicitly waits for followers before
proceeding.

**Failure scenario:** `proc_barrier::end()` only decrements a shared counter
and posts the lock mutex -- it does not block on followers. A leader whose
exec'd command exits quickly can race ahead of slow followers still opening
`/proc/<leader_pid>/ns/{user,mnt}`, causing those followers' `namespace_join`
to fail with ENOENT. Reachable via the live `uenv run --join` CLI path.

## 10. FUSE Slurm path never adopts job GID before mounting (correctness)

**`src/slurm/plugin_fuse.cpp:127`**

Ben's note: is effective GID important with squashfuse, because we are not
root (so squash-root is not in effect).

`slurm_spank_task_init_sqfs_ll()` (the FUSE-build Slurm path) never adopts
the job's effective GID before mounting, unlike `src/slurm/plugin.cpp`'s
`init_post_opt_remote()` (classic path) which calls `setegid(job_gid)` to
work around NFS root_squash denying access to group-readable images.

**Failure scenario:** A squashfs image readable only by the job's group
(mode 640) on an NFS root_squash export mounts successfully via the classic
Slurm plugin path but fails to open via the FUSE/rootless path on the same
setup, since the process never adopts the job GID needed for read access.

## 11. PR_SET_DUMPABLE never reset for FUSE squashfs-mount leader (security)

**`src/uenv/rootless.cpp:102`**

`PR_SET_DUMPABLE` is never reset to 0 for the FUSE-build squashfs-mount
leader: `map_effective_user()`'s reset is commented out and no hook
re-enables it, unlike `plugin_fuse.cpp`'s Slurm path which explicitly does
after `wait_peers()`.

**Failure scenario:** The FUSE squashfs-mount leader session (and the user
command it execs into) stays dumpable/ptraceable for its entire lifetime,
widening the ptrace/proc-access attack surface relative to the Slurm
plugin's equivalent path.

## 12. Dead code: forked child returns instead of _exit() on fail (correctness)

**`src/slurm/plugin_fuse.cpp:71`**

In the currently-dead `slurm_spank_task_init_sqfs_mount()`, the forked child
does a plain `return -ESPANK_ERROR;` instead of `_exit()` when `execlp()`
fails -- the exact fork-safety hazard `rootless.cpp`'s own `child_fail()`
comment documents.

**Failure scenario:** The call to this function is commented out today, so
it's unreachable, but it ships compiled in the plugin binary. If ever
reactivated and `execlp` fails, the forked child unwinds back into
slurmstepd's/SPANK's normal control flow instead of terminating, duplicating
task execution.

## 13. Error path treats generic void* buffer as a C string (correctness)

**`src/uenv/posix_io.cpp:25`**

`uenv::write(fd, buf, count)`'s error path formats the failure message via
`fmt::format("...{}...", (char*)buf, ...)`, reinterpreting the generic
`const void*` parameter as a null-terminated C string.

**Failure scenario:** `uenv::write()` is declared as a general-purpose
wrapper over arbitrary void*/size_t data, currently only called with
null-terminated uid_map/gid_map buffers so it happens not to misbehave. Any
future caller passing non-null-terminated or binary data whose `write()`
fails will have the error-reporting path itself read out of bounds while
building the message explaining the failure.

## 14. Debug __FILE__:__LINE__ glued onto user-facing mount error (polish)

**`src/uenv/mount.cpp:264`**

`do_mount()`'s error path appends `fmt::format("{}:{}", __FILE__, __LINE__)`
directly onto the libmount error string with no separator, unlike the other
5 error returns in the same function.

**Failure scenario:** A user-facing mount-failure message reads as the
libmount error text glued directly onto the build machine's absolute source
path, leaking build-host paths into production error output -- looks like
leftover debug instrumentation never cleaned up before merge.

## 15. --join help text copy-pasted from --view option (polish)

**`src/cli/run.cpp:29`**

The new `-j,--join` boolean flag's help text is copy-pasted from the
`-v,--view` option above it: "comma separated list of views to load".

**Failure scenario:** Running `uenv run --help` shows a factually wrong
description for `--join`, telling users it takes a comma-separated list of
views when it actually takes no argument and controls namespace-join
behavior for multi-task jobs.

---

## Lower-severity items (cut for output cap, still confirmed)

- `src/util/setns.cpp`'s non-ENOENT `open()` failure path also drops
  `strerror(errno)` (inconsistent with the rest of the diff).
- `src/uenv/rootless.cpp`'s `map_effective_user()` uses raw `sprintf` + signed
  `%d` for unsigned uid/gid where the sibling function two lines above uses
  bounded `snprintf`.
- Style-only items deprioritized below correctness bugs: missing braces per
  CLAUDE.md, undocumented `MS_SLAVE`/`MS_PRIVATE` divergence, `uid_t`/`gid_t`
  mismatches, `util/macros.h` missing include guard.

---

# Design review: squashfs-mount and Slurm plugin architecture

This is a design/architecture review, not a bug hunt: how the setuid (kernel
squashfs driver) and FUSE/rootless (squashfuse_ll) implementations are
structured today, and how to simplify them. It answers the three questions
asked directly: split into separate files, avoid `#ifdef`, and whether both
flavors can coexist in one binary at runtime.

## How it's built today

The `fuse` meson option already makes this a **compile-time**, not
runtime, choice — no built binary today supports both flavors
simultaneously:

- `meson.build` only pulls `squashfuse`/`fuse3` and compiles
  `src/uenv/rootless.cpp` into `lib_uenv` when `use_fuse` is true.
- `src/squashfs-mount/squashfs-mount.cpp` is **one file**, built once per
  configure, with `-DUENV_FUSE_MOUNT` defined only in the fuse branch of the
  `if get_option('fuse')` in `meson.build:242-260`.
- `src/slurm/meson.build` only adds `plugin_fuse.cpp` to `module_src`, and
  only defines `-DUENV_FUSE`, when `use_fuse` is true. `plugin.cpp` is
  always compiled, but its kernel-mount function
  (`init_post_opt_remote`, `src/slurm/plugin.cpp:244-303`) is wrapped in
  `#if not defined(UENV_FUSE)` and disappears entirely from fuse builds.

So the three points of divergence are:

1. **squashfs-mount CLI** (`src/squashfs-mount/squashfs-mount.cpp`) — used by
   `uenv run`/`uenv start` (`src/cli/util.cpp:squashfs_mount_args`). Invoked
   once per task (there's no single privileged parent that mounts once for
   the whole job step), so it always needs the leader-election/barrier dance
   in `mount_and_join_ns()`, for both flavors.
2. **Slurm plugin, classic/kernel path** (`src/slurm/plugin.cpp:
   init_post_opt_remote`) — runs once in the *remote* SPANK context
   (`S_CTX_REMOTE`), in slurmstepd, **before** slurmstepd forks the job's
   tasks. The mount is naturally inherited by every forked task, so this path
   needs no barrier at all.
3. **Slurm plugin, FUSE path** (`src/slurm/plugin_fuse.cpp:
   slurm_spank_task_init_sqfs_ll`) — runs in `slurm_spank_task_init`, which
   fires once *per task*, after tasks already exist as separate processes.
   It has to elect a leader among those tasks and have the rest join its
   namespaces, exactly like the squashfs-mount CLI case above.

This is the important design fact the current code doesn't say out loud
anywhere: **the two flavors don't just call a different `mount()` — they
have different synchronization models**, driven by *where in the Slurm
lifecycle privilege is available*. The kernel path gets privilege for free
(slurmstepd runs as root, mounts once, before forking); the FUSE path never
has a privileged single point, so every entry point that uses it needs the
barrier/leader/join machinery. That's why the barrier logic is duplicated
between `squashfs-mount.cpp` and `plugin_fuse.cpp` rather than needed
everywhere.

## 1. Split into separate implementation files? Yes.

`squashfs-mount.cpp` currently interleaves three genuinely different
concerns behind one `#ifdef UENV_FUSE_MOUNT`:

- CLI parsing, environment forwarding, exec'ing the target command — 100%
  shared, correctly so.
- Three "hook" lambdas (`setup_sandbox`, `mount_sqfs`, `exit_sandbox`) that
  each have a completely different body per flavor
  (`squashfs-mount.cpp:214-234`).
- A `namespaces_to_join` choice inside `mount_and_join_ns()`
  (`squashfs-mount.cpp:72-77`) that is *also* flavor-specific, but is
  expressed as a runtime `is_setuid()` branch instead of living next to the
  other two flavor-specific hooks.

Recommended split:

- `src/squashfs-mount/main.cpp` — CLI parsing, environment forwarding, the
  barrier/join call, exec. Zero `#ifdef`. Depends only on a small interface,
  e.g.:
  ```cpp
  // sandbox.h — implemented once per build flavor
  util::expected<void, std::string> setup_sandbox();
  util::expected<void, std::string> mount_sqfs(const uenv::mount_pair&);
  util::expected<void, std::string> exit_sandbox(uid_t uid);
  std::vector<std::string> follower_namespaces_to_join();
  ```
- `src/squashfs-mount/sandbox_kernel.cpp` — today's `#else` branch
  (`unshare_and_become_root`/`do_mount`/no-op exit), compiled only when
  `fuse=false`.
- `src/squashfs-mount/sandbox_fuse.cpp` — today's `#ifdef` branch
  (`unshare_mount_map_root`/`do_sqfs_ll_mount`/`exit_sandbox`), compiled
  only when `fuse=true`.
- `meson.build` picks exactly one of `sandbox_kernel.cpp`/`sandbox_fuse.cpp`
  as a source alongside the always-present `main.cpp` — the same pattern
  already used for `src/slurm/meson.build`'s `plugin.cpp` +
  `plugin_fuse.cpp`, which is worth pointing at as the existing precedent to
  follow, not diverge from.

Apply the same idea to the Slurm plugin, which is *already* halfway there
but undermined by one leftover `#ifdef`:

- Move `init_post_opt_remote` out of `plugin.cpp` into a new
  `plugin_kernel.cpp`, and delete the `#if not defined(UENV_FUSE)` guard
  from `plugin.cpp` (both occurrences, lines 234/305 and 544/548) entirely.
  `plugin.cpp` keeps only the genuinely shared option registration,
  local/allocator logic, and the `slurm_spank_init_post_opt` dispatcher,
  which forward-declares `impl::init_post_opt_remote` and trusts the linker
  to supply it from whichever kernel/fuse file was compiled — exactly how
  `slurm_spank_task_init` already works today (defined only in
  `plugin_fuse.cpp`, simply absent from the symbol table in kernel builds).
  `src/slurm/meson.build` then adds `plugin_kernel.cpp` unconditionally when
  `plugin_fuse.cpp` isn't added, instead of relying on an `#ifdef` inside a
  file that's always compiled.
- Delete `slurm_spank_task_init_sqfs_mount()` in `plugin_fuse.cpp`
  (lines 35-124). It is dead code — the only call site is commented out
  (`plugin_fuse.cpp:26`) — and it hand-duplicates the barrier/namespace-join
  dance that the live `slurm_spank_task_init_sqfs_ll()` also implements, with
  a different (and, per the earlier bug review, buggier) fork-exec-based
  approach. Removing it drops ~90 lines and the file's only remaining
  function is the one actually reachable.

## 2. Avoid the `#ifdef`s sprinkled through the code? Yes, almost entirely.

After the split above, the only preprocessor conditionals left in this area
are ones that aren't actually about the setuid/FUSE choice and should stay:

- `#ifdef SLURM_VERSION_NUMBER` (`plugin.cpp:50`) — Slurm API compatibility,
  unrelated.
- `#ifdef SQFS_MULTITHREADED` / `#if FUSE_USE_VERSION >= 30`
  (`rootless.cpp:231-244`) — squashfuse/libfuse API version compatibility,
  internal to `sandbox_fuse.cpp`'s own concerns either way.

Every `#ifdef UENV_FUSE_MOUNT` / `#if not defined(UENV_FUSE)` instance goes
away, because the choice becomes "which `.cpp` file did meson compile",
resolved once at the source-list level in `meson.build`, rather than
resolved repeatedly inline throughout function bodies.

One more cleanup this split makes easy: `is_setuid()` is currently used for
two unrelated purposes that happen to look the same — (a) a genuine runtime
safety check ("refuse to run FUSE code under a setuid bit",
`squashfs-mount.cpp:101-105`), and (b) a stand-in for "which build flavor is
this" (`squashfs-mount.cpp:72`, `:140`, `:274`). Once flavor is a
compile-time fact again (one file per flavor), only use (a) remains, and it
should stay — it's a legitimate defensive assertion, not a behavior switch.
This distinction isn't cosmetic: finding #1 in the bug review above
(the privilege-drop that's silently skipped) exists *because* `is_setuid()`
was used as a flavor proxy at `squashfs-mount.cpp:274`, checked *after*
`setup_sandbox()` had already changed the very uids the check inspects, so
it always evaluates false regardless of which build produced the binary.
Splitting the files removes the temptation to use a uid check as a proxy for
a fact the linker already knows.

## 3. Runtime dual-mode — both flavors compiled in, chosen by setuid at runtime? No.

This is worth rejecting explicitly rather than leaving unconsidered,
because it's the most "flexible"-looking option and the one a generic
"reduce build variants" instinct would reach for.

**For `squashfs-mount` specifically, this is actively worse, not just
unnecessary:** the setuid-installed binary is the one component in this
codebase that runs with real privilege handed to it by the kernel before a
single line of `main()` executes. Everything else in this repo's dependency
story (see CLAUDE.md's OpenSSL/static-linking notes) is already organized
around minimizing what a privileged/networked binary links in. Compiling
squashfuse + fuse3 into the setuid binary and merely gating their use behind
a runtime `is_setuid()` check means that code is still linked, still mapped
into the setuid process's address space, and still reachable by anything
that can influence control flow (a memory-safety bug, a logic-gate bypass
like the one in finding #1) — regardless of whether the "normal" code path
ever calls it. A setuid binary should be as small and as auditable as the
mounting task requires, and nothing more; today's compile-time exclusion
already achieves that, and it should be kept, not traded for flexibility
nobody asked for. The Slurm plugin runs as root in the remote context for
the same reason: no benefit to shipping FUSE code into `slurm-uenv-mount.so`
on clusters that only ever use the kernel path.

There's also no real use case for it: a given deployment (a given cluster,
a given install) picks one mounting strategy and sticks with it — nothing
in the design toggles between kernel and FUSE mounting *within* one running
system. `is_setuid()` as it exists today already gives you the one runtime
distinction that's actually needed (the FUSE binary refusing to run
setuid), and that check is cheap to keep as a guard clause in
`sandbox_fuse.cpp`'s `setup_sandbox()` without requiring the kernel code to
be compiled in at all.

## 4. One more consolidation worth doing: shared barrier/join helper

`mount_and_join_ns()` (`squashfs-mount.cpp:44-92`) and
`slurm_spank_task_init_sqfs_ll()`'s barrier section
(`plugin_fuse.cpp:159-236`) implement the same five-step protocol by hand,
independently: create barrier → leader mounts + `ready()` → followers
`namespaces_join()` + `signal_done()` → (leader `wait_peers()`) → `end()`.
They differ only in how they report errors (`error_and_exit` vs
`slurm_error`) and in incidental details — which is exactly how finding #9
in the bug review (missing `wait_peers()` in the squashfs-mount leader path)
came to exist in one copy but not the other. Factoring this into a single
helper in `src/util/` (e.g.
`util::leader_mount_and_join(tag, ntasks, mount_fn, namespaces_to_join,
on_error)`) means there is one place to get the protocol right, and one
place to fix it when a bug like #9 is found, instead of two.

## Summary

| Question | Answer |
|---|---|
| Split setuid/FUSE into separate files? | Yes — `main.cpp` + `sandbox_kernel.cpp`/`sandbox_fuse.cpp` for squashfs-mount; carve `plugin_kernel.cpp` out of `plugin.cpp` for the Slurm plugin, matching the `plugin_fuse.cpp` precedent that already exists. |
| Avoid `#ifdef` in the source? | Yes — the flavor choice moves entirely into `meson.build`'s source lists; the only conditionals left are unrelated Slurm/FUSE API-version compatibility shims. |
| Compile both in, switch at runtime on setuid? | No — reject deliberately. It would link privilege-irrelevant code into a setuid binary for no operational benefit, and the existing bug report shows exactly the failure mode (a uid-based flavor proxy silently going stale) that this design avoids by construction. |
| Extra win | Deduplicate the barrier/leader/join protocol into one `src/util/` helper shared by both call sites, and delete the dead `slurm_spank_task_init_sqfs_mount()` in `plugin_fuse.cpp`. |
