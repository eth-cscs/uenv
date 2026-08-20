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

