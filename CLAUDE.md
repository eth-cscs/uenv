# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) and other coding
agents when working with code in this repository. It is the single source of
truth; `AGENTS.md` is a symlink to this file.

## Project Overview

uenv2 is a C++20 rewrite of uenv, a tool for managing user environments on HPC systems (specifically CSCS Alps). It provides:
- CLI tool (`uenv`) for managing and running environments from SquashFS images
- Slurm plugin for environment integration with job scheduling
- Optional `squashfs-mount` helper for mounting SquashFS images, built with one
  of two backends chosen at compile time — a setuid kernel-driver backend, or
  a rootless FUSE backend (see "Mounting backends: kernel vs FUSE" below)

The software is deployed as static binaries.
All environment modifications must be done via `uenv run`, `uenv start`, or Slurm integration.

## Build System

This project uses **Meson** (>= 1.4) as its build system with **Ninja** as the backend.

### Building

```bash
# Configure build (from repository root)
mkdir build && cd build
meson setup -Dtests=enabled ..

# Compile
meson compile

# Install (requires sudo for system installation)
sudo meson install --no-rebuild --skip-subprojects

# Install to staging directory (for testing)
sudo meson install --destdir=$PWD/staging --no-rebuild --skip-subprojects
```

### Build Options

Configure via `-Doption=value` with `meson setup`:
- `tests=enabled|disabled` - Enable test suite (default: disabled)
- `cli=true|false` - Build CLI tool (default: true)
- `slurm_plugin=true|false` - Build Slurm plugin (default: true)
- `squashfs_mount=true|false` - Build squashfs-mount helper (default: false)
- `mount_backend=kernel|fuse` - Which mounting backend the CLI, Slurm plugin,
  and squashfs-mount helper are all built against (default: `kernel`). A
  compile-time, exclusive choice — a single build links one backend, never
  both. See "Mounting backends: kernel vs FUSE" below. `squashfs_mount`'s
  install mode also depends on this: setuid (`rwsr-xr-x`, root-owned) for
  `kernel`, plain executable for `fuse`.

### Subproject options and stale build directories

The `default_options` passed to a `subproject()` call in `meson.build` are applied
**only the first time that subproject is configured**. Editing them and running
`meson setup --reconfigure` does *nothing*: the build directory keeps the option
values it was created with, the build succeeds, and the only symptom is a binary
that was built the old way.

This matters most for the `curl` subproject, where every feature is pinned
explicitly so that the shipped binary has no runtime dependencies beyond libc,
libstdc++, libm and libgcc_s. Most of curl's options default to `auto`, meaning
they switch on wherever the build host happens to have the matching dev package
installed — leaving any of them unpinned makes `ldd` output a property of the
build machine rather than of the source tree.

After changing a subproject's options, recreate the build directory:

```bash
meson setup --wipe build          # or: rm -rf build && meson setup ... build
```

Check the result rather than assuming it took — `meson setup` prints a feature
summary per subproject, and the finished binary should show only the four system
libraries:

```bash
ldd build/uenv
```

CI and the RPM build always start from a fresh directory, so they are unaffected;
this only bites developers with a long-lived local build directory.

Note that `meson setup --wipe` deletes the whole build directory, which fails if
it contains a root-owned `staging/` from an earlier
`sudo meson install --destdir=...`. Remove that with `sudo` first.

## Testing

Six test suites exist:
1. **unit** - C++ unit tests using Catch2 (in `test/unit/`)
2. **cli** - CLI integration tests using BATS (in `test/integration/cli.bats`)
3. **completion** - tab completion (`uenv __complete` and the bash and zsh scripts) using BATS (in `test/integration/completion.bats`)
4. **slurm** - Slurm plugin tests using BATS (in `test/integration/slurm.bats`)
5. **squashfs-mount** - setuid helper tests using BATS (in `test/integration/squashfs-mount.bats`)
6. **registry** - `uenv push`/`pull` against a throwaway local zot registry (in `test/integration/registry.bats`); self-skips when no zot binary is available

### Running Tests

To run the tests, run the tests directly instead of running them through meson.

```bash
# Run tests directly
./test/unit                      # Unit tests
./test/bats ./test/cli.bats      # CLI tests
./test/bats ./test/completion.bats # Tab completion tests
./test/bats ./test/slurm.bats    # Slurm tests
./test/bats ./test/registry.bats # Registry (push/pull) tests
```

### System name in tests

Most CLI/Slurm tests resolve uenvs by a bare label (`app/42.0`, `tool`) and rely
on the default system being `arapiles` (the repo records are stored `@arapiles`).

The default system name comes from the config layers, merged in this order (later
wins): `CLUSTER_NAME` env var → system config (`/etc/uenv/config.toml`) → user
config (`$XDG_CONFIG_HOME/uenv/config.toml` or `$HOME/.config/uenv/config.toml`) →
the `--system` CLI flag. On Alps the deployed system config sets
`system_name = 'eiger'`, which **overrides `CLUSTER_NAME=arapiles`**. So exporting
`CLUSTER_NAME` in a test is not sufficient.

To force the system name, `cli.bats` and `slurm.bats` `setup()` write a throwaway
user config that sets `system_name = 'arapiles'` and point `XDG_CONFIG_HOME` at it
(user config beats system config). A test that writes its own `config.toml` must
include `system_name = 'arapiles'` (or append to the file created in `setup()`
with `>>` rather than clobbering it with `>`). Alternatively, pass `--system` on
the command line.

### Elastic mock (`elastic_mock`)

The slurm tests include an elastic telemetry test that uses a standalone mock server at `test/integration/elastic_mock`. The meson build copies it to `$BUILD_PATH/test/elastic_mock` (with execute permissions), and `setup_suite.bash` adds `$BUILD_PATH/test` to `PATH` so it is available by name in all BATS tests.

The script supports subcommands and is also useful for manual testing:

```bash
# pick a free port, start the server (backgrounds itself with &)
port=$(elastic_mock free-port)
elastic_mock serve /tmp/cap.json "$port" &
elastic_mock wait-server "$port"        # wait until accepting connections

# send a request and inspect results
curl -s -X POST http://127.0.0.1:$port -d '{"name":"tool"}'
elastic_mock count /tmp/cap.json        # → 1
elastic_mock get /tmp/cap.json          # pretty-print last record
elastic_mock assert /tmp/cap.json name tool   # exits 0
elastic_mock assert /tmp/cap.json name wrong  # exits 1 + diagnostic

# stop the server (or use `kill %1` / stop_elastic_mock in BATS)
elastic_mock kill /tmp/cap.json
```

In BATS tests the helper functions in `common.bash` wrap the common lifecycle: `start_elastic_mock CAPTURE_FILE PORT` (backgrounds `serve` and calls `wait-server`), `stop_elastic_mock` (kills by PID), and `wait_elastic_post CAPTURE_FILE [TIMEOUT]`.

### Registry mock (`registry_ctl`, `listing_mock`)

The `registry` suite exercises the native OCI client (`src/oci`) end-to-end against
a real registry, without containers or the old `oras` binary. Two helper scripts
are built into `$BUILD_PATH/test` (and so are on `PATH` in all BATS tests):

- `registry_ctl` — manages the lifecycle of a throwaway [zot](https://zotregistry.dev)
  registry (a single static binary). Subcommands include `runtime` (report
  the zot binary, empty if unavailable → suite self-skips), `free-port`, `serve
  STATE PORT` (backgrounds itself), `wait PORT --timeout N`, `digest PORT REPO REF`,
  and `kill STATE`.
  The zot binary is fetched at build time by `test/integration/install-zot`.
- `listing_mock` — stands in for the CSCS uenv listing service
  (`https://uenv-list.svc.cscs.ch/list`); same `free-port`/`serve`/`wait-server`/`kill`
  lifecycle. Point uenv at it with `registry.listing_url` in the config.

`registry.bats` drives `registry_ctl serve` / `listing_mock serve` directly from
its `setup_file` (the servers must outlive individual tests, so their state is
exported rather than held in `common.bash` helper vars). It writes a user
`config.toml` whose `[registry]` block sets `url`, `default_namespace`, and
`listing_url` to point at the local zot + listing_mock. The `[registry]` unit tests
in `test/unit/oci_registry.cpp` cover the OCI round-trip directly by starting their
own zot.

### Testing squashfs-mount

The `squashfs-mount` helper requires setuid installation to test:

```bash
# Set up staging path
export STAGE=$PWD/staging
export STAGING_PATH=$STAGE/usr/local

# Build with squashfs-mount enabled
meson setup -Dtests=enabled -Dsquashfs_mount=true
meson compile

# Install as root (for setuid bit)
sudo meson install --destdir=$STAGE --no-rebuild --skip-subprojects

# Run tests (STAGING_PATH tells tests where to find the setuid binary)
./test/bats ./test/cli.bats
./test/bats ./test/squashfs-mount.bats
```

**IMPORTANT**: Never build with sudo (`sudo meson compile` or `sudo ninja`). Always build as normal user, then install with sudo.

A few cases in `squashfs-mount.bats` and `slurm.bats` need a *root-owned*,
mode-600 image as a fixture, to check that the privileged mount paths refuse an
image the calling user cannot read (see "Who opens the image (kernel backend)").
They create it with `sudo -n` and `skip` when passwordless sudo is unavailable,
so they are silently skipped in CI and in a normal developer run — if you are
changing that code path, check they are actually running rather than skipping.

## Architecture

### Source Structure

- `src/cli/` - CLI command implementations (add_remove, build, completion, config, copy, delete, find, help, image, inspect, ls, pull, push, repo, run, start, status), the tree of commands they are registered in (`cli.h/cpp`), and tab completion (`complete.h/cpp`, and the shell scripts in `src/cli/completion/`). See "Tab completion" below.
- `src/argparse/` - Command line argument parser used by the CLI and `squashfs-mount`. See "The `argparse` command line parser" below.
- `src/uenv/` - Core library shared between CLI and Slurm plugin
  - Environment management (`env.h/cpp`, `uenv.h/cpp`)
  - Repository/database operations (`repository.h/cpp`)
  - Parsing (`parse.h/cpp`, `lex.h` in util)
  - Mounting (`mount.h/cpp`, plus exactly one of `mount_kernel.h/cpp` or
    `mount_rootless.h/cpp` — chosen by the `mount_backend` build option, see
    "Mounting backends: kernel vs FUSE" below)
  - Multi-task rendezvous tag/count derivation for the FUSE backend
    (`join_context.h/cpp`)
  - Meta data (`meta.h/cpp`)
  - Views (`view.h/cpp`)
  - Telemetry (`telemetry.h/cpp`, `elastic.h/cpp`)
  - Logging (`log.h/cpp`, `print.h/cpp`)
  - Settings management (`settings.h/cpp`)
  - Tab completion of labels, uenv lists, views, repos and paths (`complete.h/cpp`)
- `src/oci/` - Native OCI registry client (container registry interaction: pull, push, copy, manifests, auth); replaces the external `oras` binary. See "Self-contained `src/oci`" below.
- `src/util/` - Utility libraries (color, curl, envvars, fs, lex, lustre, privilege, semver, shell, signal, strings, subprocess, toml, unique_fd), plus the FUSE backend's IPC/process-coordination primitives (`proc_barrier.h/cpp`, `named_semaphore.h`, `shared_mapping.h`, `robust_mutex.h`, `setns.h/cpp`, `ready_fork.h/cpp`) — see "Multi-task rendezvous and IPC error model" below
- `src/site/` - Site-specific configuration (CSCS-specific logic)
- `src/slurm/` - Slurm plugin implementation; `plugin_kernel.cpp` or `plugin_fuse.cpp` is compiled in depending on `mount_backend`
- `src/squashfs-mount/` - Helper for mounting SquashFS images; built from `squashfs-mount-kernel.cpp` (installed setuid) or `squashfs-mount-fuse.cpp` (installed as a plain executable) depending on `mount_backend`

### Key Concepts

**uenv_label**: Represents a uenv identifier with optional fields:
- Format: `name/version:tag@system%uarch`
- Example: `prgenv-gnu/24.11:v2@daint%gh200`

**uenv_description**: Describes a uenv either by label or by filename, with optional mount point.

**concrete_uenv**: Fully resolved uenv with paths (mount_path, sqfs_path, meta_path) and loaded metadata.

**repository**: SQLite-backed database tracking available uenvs. Operations: query, add, remove, contains.

**view**: Named environment configurations within a uenv (stored in env.json metadata).

### Data Flow

1. User provides uenv description (label or file path)
2. Parse into `uenv_description` using `parse.h` functions
3. Resolve to `concrete_uenv` (find squashfs image, mount location, metadata)
4. Load view from metadata (env.json) and configure update to the environment variables
    - the CLI performs this by updating the environment variable store that is used to create `environ` for the call to exec in step 5
    - the Slurm plugin sets environment variables using setenv/getenv in the local context, and letting Slurm forward the environment to the remote context
5. Mount squashfs image at mount point
    - the CLI does this by execing the `squashfs-mount` helper, which in turn runs step 6 below
    - the Slurm plugin performs the mount in the remote context before the daemon forks the MPI processes
    - on the `kernel` backend, both paths go through the setuid helper as root; on the `fuse` backend, the mount happens rootlessly in a user/mount namespace, and if multiple Slurm tasks share the node, only one ("the leader") actually mounts — see the next two sections
    - on the `kernel` backend the image is always **opened with the calling user's credentials** and only then mounted as root — see "Who opens the image (kernel backend)" below
6. Execute command with environment from view

### Mounting backends: kernel vs FUSE

Two mutually exclusive backends implement the actual squashfs mount, selected
by the `mount_backend` build option (`kernel` default, or `fuse`). A single
binary only ever contains one — `meson.build` compiles in exactly one of each
pair (`mount_kernel.cpp`/`mount_rootless.cpp` into the shared library,
`plugin_kernel.cpp`/`plugin_fuse.cpp` into the Slurm plugin,
`squashfs-mount-kernel.cpp`/`squashfs-mount-fuse.cpp` into the helper binary).
There is no runtime switch between them.

- **`kernel`**: mounts the squashfs image via the kernel's loop-device +
  squashfs driver, using the loop ioctls (`LOOP_CTL_GET_FREE`,
  `LOOP_CONFIGURE`) and `mount(2)` directly. libmount is deliberately not
  used: from util-linux 2.42 it treats any setuid process as "restricted"
  and refuses to mount without a matching `/etc/fstab` entry. Requires root
  for the mount syscalls. The CLI
  path (`uenv run`/`uenv start`) is an unprivileged process, so it execs the
  setuid `squashfs-mount` helper to get there. The Slurm plugin does not go
  through that helper at all: its mount runs inside
  `slurm_spank_init_post_opt` in the remote context (`src/slurm/plugin_kernel.cpp`),
  a hook Slurm itself invokes as root, so `uenv::do_mount()` is called
  directly with the privilege the plugin already has. Neither path opens the
  image with that privilege — see the next section.
- **`fuse`**: mounts via `squashfuse_ll` inside a fresh user + mount
  namespace, entirely unprivileged — no setuid bit needed
  (`src/uenv/mount_rootless.cpp`). Since Slurm can co-locate several tasks
  from the same job/step on one node, and each task's namespace is otherwise
  private, the tasks that share a node rendezvous around a single leader's
  mount (elected among themselves) and the rest join that leader's
  namespaces with `setns()` rather than each mounting independently. That
  rendezvous is what the next section documents.

### Who opens the image (kernel backend)

Both privileged entry points — the setuid helper and the root SPANK hook —
mount an image that an unprivileged user named. The rule that keeps that safe:

**the image is opened with the calling user's credentials; only the loop-device
and `mount(2)` syscalls run as root.**

`LOOP_CONFIGURE` binds a *file descriptor*, not a path, and DAC is checked at
`open(2)` only. So `uenv::open_images()` (`src/uenv/mount_kernel.cpp`) opens
each image as the user, and `do_mount()` then binds those descriptors as root.
The mount therefore inherits exactly the user's authority over the image — file
modes *and* directory traversal — and there is no window in which the path
could be swapped, because the path is never re-opened. `open_images()` enforces
this rather than documenting it: it refuses to run when the effective user is
root and the real user is not.

`parse_and_validate_mounts()` runs inside the same unprivileged window at both
sites. That is deliberate and must not be moved back above the elevation: as
root, its `weakly_canonical`/`is_regular_file`/`file_size`/magic-read sequence
is an existence/type/first-4-bytes oracle for arbitrary paths.

Every change to a process's uids and gids, and the read-back verification of
each change, lives in `src/util/privilege.{h,cpp}`: `util::ids` is a uid/gid
pair, `util::process_ids` the real/effective/saved triple plus supplementary
groups, and `util::user_ids` a user's uid, gid and groups. `mount_kernel.cpp`
contains no `set*id` calls; it only elevates to the mount namespace with
`uenv::unshare_mount_namespace()`, which both privileged sites call once they
are root.

How each site reaches the user's credentials differs, and the difference is all
about gids:

- **Setuid helper** (`squashfs-mount-kernel.cpp`): `util::set_effective_uid()`
  to the real uid at the top of `main()`, and that is all that is needed. The
  binary is setuid but not setgid, so the egid and the supplementary groups are
  *already* the caller's — which is both what makes the check complete and what
  the exec'd command must keep. **Never add `setegid()`/`setgroups()` here.**
  Privilege comes back in `util::become_root()`, which `seteuid(0)`s and then
  `setreuid(0, 0)`s *before* the namespace is unshared: dropping euid from 0
  clears the effective capability set, so `unshare(CLONE_NEWNS)` would
  otherwise fail `EPERM`. It is given up for good in
  `util::drop_privileges()`, gid before uid and both with `setres*id`, because
  the saved-set-gid survives `execve` and `PR_SET_NO_NEW_PRIVS` does not prevent
  regaining a saved id.
- **Slurm plugin** (`plugin_kernel.cpp`): slurmstepd is full root with *root's*
  supplementary groups, so all of `S_JOB_UID`, `S_JOB_GID` and
  `S_JOB_SUPPLEMENTARY_GIDS` must be assumed together — the uid alone leaves
  root's groups in the credential set (too permissive) while omitting the job's
  groups refuses images the user reaches through a project group (too
  restrictive, and a common Alps case). It uses `util::run_as_user()`
  (`src/util/privilege.cpp`), which does the drop **on a dedicated thread using
  raw syscalls**. Credentials are per-task on Linux; it is glibc that broadcasts
  `setuid`/`setgid` to every thread. Confining the change to one thread leaves
  slurmstepd itself root, which matters because this hook runs before Slurm
  drops privileges, with the step's message thread already serving RPCs, and
  Slurm serialises its own euid changes behind `auth_setuid_lock()` — a lock a
  SPANK plugin cannot take. Do not "clean up" those raw syscalls into the glibc
  wrappers: that silently turns a one-thread change into a whole-process one.

A consequence worth knowing when debugging a site: an image that only root can
read, or one under a directory the user cannot traverse, is now refused. That is
the point of the check, but it does mean an image store deployed with
restrictive modes will stop working where it used to.

### Mount lifetime (FUSE backend)

Who owns a mount's lifetime differs between the solo and the `--join` path,
because what the mount should die with differs.

- **Solo (`ntasks == 1`: `uenv run`, `uenv start`)**: the squashfuse daemon is
  `PR_SET_PDEATHSIG`-tied to the process that goes on to `exec` the command.
  There is exactly one user, and this path also has to work outside Slurm (a
  login node), where nothing else would clean up after it.
- **`--join` (`ntasks > 1`)**: the leader forks a **mount supervisor**
  (`fork_mount_supervisor()`/`supervisor_main()` in `mount_rootless.cpp`) which
  forks the daemons, reports readiness over a `util::ready_fork` pipe, detaches
  its stdio and then sleeps forever. The daemons are `PDEATHSIG`-tied to *it*,
  not to any rank.

  The leader is an arbitrary rank, so anchoring the daemons to the leader's
  command — as the code originally did — took the uenv away from every other
  rank on the node the moment that one rank finished, reported only as
  `Transport endpoint is not connected`.

  **What ends the supervisor is Slurm, and that is deliberate.** `--join` only
  exists inside a job step; every process a step creates is in the step's
  cgroup, membership is inherited across `fork` and cannot be escaped from user
  space, and `slurmstepd` `SIGKILL`s whatever remains in that cgroup as soon as
  the last task exits — measured at ~3 ms on Alps, including for a
  signal-ignoring process in its own namespaces. The mount's lifetime is
  therefore the step's lifetime on that node. Forks, orphans, nested containers
  and fd-closing commands are all irrelevant to it, unlike any scheme where
  uenv tries to work out for itself whether anyone still needs the mount.

  Three consequences that must not be "tidied up":

  - the supervisor arms **no** `PR_SET_PDEATHSIG` of its own — outliving the
    task that forked it is the point;
  - it must **never** `setsid()` or otherwise leave the cgroup it was forked
    into, since that cgroup is the entire termination mechanism;
  - it **ignores `SIGTERM`/`SIGINT`/`SIGHUP`**. On a time limit or `scancel`,
    Slurm signals every process in the cgroup and *then* gives the ranks
    `KillWait` seconds to shut down cleanly; a supervisor that died on that
    signal would pull the mount out from under ranks that are still flushing or
    checkpointing.

  It also detaches stdio once the mounts are up, so a process outliving the
  ranks can never hold the step's stdout/stderr pipes open. Mount failures are
  reported before that point.

  Because that termination mechanism is Slurm's rather than uenv's, the leader
  checks for it instead of assuming it: `util::cgroup_is_slurm_managed()`
  (`src/util/cgroup.h`) looks for Slurm's `/job_<id>/` component in
  `/proc/self/cgroup`, present in both the cgroup v1 and v2 layouts. Where it
  is absent — a site running `proctrack/linuxproc` or `proctrack/pgid`, where a
  process reparented away from its task is not reliably reaped — the leader
  warns and mounts in-process instead, falling back to the solo model. A leaked
  fuse daemon and the namespace pinning its image is worse than the mount
  ending with the leader's command.

  Observed on Alps (Slurm 25.05.4, `proctrack/cgroup`, cgroup v2,
  `PrologFlags=...,Contain`): the supervisor lands in
  `.../slurmstepd.scope/job_<id>/step_<n>/user/task_0`. Re-check this on a new
  system or Slurm version.

### Multi-task rendezvous and IPC error model (FUSE backend)

`src/util/proc_barrier.{h,cpp}` implements the rendezvous the `fuse` backend
uses to elect one mount leader among the Slurm tasks sharing a node
(`src/uenv/mount_rootless.cpp`). It's built from POSIX IPC objects named
after a `tag` string: two named semaphores (`bootstrap`, serializing the
leader-election race; `done`, counting followers finished acting on the
leader) plus one POSIX shared-memory segment holding a `robust_mutex`
(`src/util/robust_mutex.h`, `PTHREAD_PROCESS_SHARED | PTHREAD_MUTEX_ROBUST`)
and the peer count/leader pid. `tag` and the peer count (`nprocs`) are always
derived together from the same source (`uenv::local_join_context()` in
`src/uenv/join_context.h`, scoped to `SLURM_JOBID`-`SLURM_STEPID`) — a tag
must never be paired with a peer count computed some other way, or unrelated
groups can collide on the same IPC names.

**The governing correctness rule for this whole subsystem: if any one peer
fails while the barrier is still being set up, the entire rendezvous must
fail for every peer — never let the survivors proceed on fewer than
`nprocs`.** Two mechanisms enforce this, for the two points at which a peer
can fail:

- **After the leader has taken `setup` (the robust mutex) and started
  setup**: if it dies (SIGKILLed, OOM-killed, hits a job time limit — all
  ordinary HPC events), the kernel's robust-mutex machinery reports
  `EOWNERDEAD` to the next `lock()` instead of wedging forever. The peer that
  observes it deliberately does *not* call `pthread_mutex_consistent()`, so
  the mutex stays permanently `ENOTRECOVERABLE` — every peer, present or
  future, that touches this tag's mutex fails cleanly rather than one
  quietly resuming on state nobody finished writing.
- **Before that point** — `create_exclusive()`, `setup.init()`, or the
  leader's first `setup.lock()` failing (all node-resource-exhaustion class:
  `ENOSPC`/`EMFILE`/`ENOMEM`) — there is no mutex yet to report anything, so
  `proc_barrier::create()` deliberately leaves the `bootstrap` semaphore held
  at 0 on every one of those error paths instead of posting it back (see the
  comment at its `wait()` call in `proc_barrier.cpp`). Every other peer,
  whenever it arrives — even long after a transient cause has cleared —
  blocks on its own `wait()` and times out (`barrier_timeout`, 30s) rather
  than racing past this point to a rendezvous that no longer includes the
  failed peer.

**Do not "fix" either of these by releasing the held resource on error.**
Both look, in isolation, like a plain resource leak: an IPC object acquired
and never released on an early-return error path. The natural-looking fix —
post the semaphore back, or `pthread_mutex_consistent()` the mutex, so the
next peer isn't blocked by this one's failure — is wrong here, because it
lets a peer that arrives *after* the failure retry the election and succeed
alongside the survivors, while the peer that actually hit the error has
already returned its own error and exited. The barrier would then quietly
complete with fewer than `nprocs` peers, which is exactly the outcome the
governing rule above forbids. The held resource being unreleased is what
stops that: it is the mechanism, not an oversight. Before changing any error
path in this subsystem (`proc_barrier.cpp`, `shared_mapping.h`,
`robust_mutex.h`, `setns.cpp`), check whether the group's all-or-nothing
guarantee depends on that path leaving the resource held before treating it
as a leak to close.

The accepted cost of both mechanisms is abandoned `/dev/shm` objects
(semaphores + the shared-memory segment) for a tag whose barrier failed this
way, plus up to `barrier_timeout` of latency. This is bounded, not an
unbounded leak: a tag is scoped to one job/step and is never reused within a
job, so nothing ever looks the abandoned names up again; at CSCS specifically
the Slurm prologue clears `/dev/shm` before every job, bounding it further.

### Dependencies

All dependencies are built as static libraries via meson wrap:
- fmt - formatting library
- spdlog - logging
- nlohmann_json - JSON parsing
- sqlite3 - database
- libcurl - HTTP operations
- zlib - gzip handling in the native OCI registry client (`src/oci`)
- libarchive - tar packing/unpacking of the `uenv/meta` artifact, in-process via `src/util/archive.*` (replaces the external `tar`/`gzip` binaries)
- tomlplusplus (toml++) - TOML parsing for the config files; carries a local patch, see "The toml++ subproject is patched" below
- Catch2 - testing (when tests enabled)
- barkeep - progress indicators (header-only in `extern/`)
- OpenSSL - the TLS backend for libcurl; no uenv code calls it directly

### The OpenSSL subproject is not a meson build

Every other dependency above is built by meson. OpenSSL is not: upstream ships no
meson build, and wrapdb's third-party port is frozen at 3.0.8 (Feb 2023), a series
whose security support ends 2026-09-07. `subprojects/openssl.wrap` is therefore
ours rather than wrapdb's, and `subprojects/packagefiles/openssl/` drives
OpenSSL's own `Configure` + `make`.

Consequences worth knowing:

- **`perl` and `make` are build requirements**, in addition to meson/ninja/g++.
- OpenSSL is configured *and compiled* during `meson setup`, not `meson compile`
  (about 10-40 s on a cold build directory, a second on a warm one). It has to be:
  most of OpenSSL 3.x's public headers are generated from `.h.in` templates, and
  curl probes `openssl/ssl.h` while *it* is being configured; and a `custom_target`
  output cannot be linked into the installed `libcurl` static library.
- Because OpenSSL is linked statically, no distro update can ever patch it. The
  version users run is the version built into the release, so bumping it is a
  security task, not housekeeping. CI asserts the exact version to keep that
  deliberate.
- **Bumping the version**: change the URL, filename, directory and hash in
  `subprojects/openssl.wrap` (verify the hash against the `.sha256` published
  beside the release asset), the `version:` in
  `subprojects/packagefiles/openssl/meson.build`, and the version assertion in
  `.github/workflows/build_and_test.yml`. Nothing else should need to change.
- **Editing `subprojects/packagefiles/openssl/*` has no effect on an already
  extracted subproject** - the packagefiles are copied over the unpacked tarball
  only when it is first extracted. Delete `subprojects/openssl-<version>/` to pick
  changes up.

### The toml++ subproject is patched

`subprojects/tomlplusplus.wrap` applies
`subprojects/packagefiles/tomlplusplus-3.4.0-parser-fixes.diff` (a `diff_files`
entry) when the tarball is extracted. The header of the diff describes the
parser defects it fixes - assertions on unexpected input, a stack overflow and
an unreachable branch, all reachable from a user-written `config.toml` and all
found by fuzzing. Regression tests live in `test/unit/settings.cpp` ("read config files
v2 malformed") and `test/integration/cli.bats` ("malformed user config").

- **Editing the diff has no effect on an already extracted subproject**: delete
  `subprojects/tomlplusplus-3.4.0/` and run `meson setup --reconfigure build`
  to re-extract and re-apply it (the tarball is cached in
  `subprojects/packagecache`).
- **Bumping toml++** means re-checking each hunk against the new version and
  dropping the ones upstream has fixed; a hunk that no longer applies fails the
  extraction.
- `subprojects/.gitignore` anchors the `tomlplusplus-*` pattern so that the
  diff under `packagefiles/` is tracked.

## Development Notes

### Code Style

Always use braces `{}` on `if`, `for`, `while`, and other control flow statements, even for single-statement bodies.

Do not use banner-style section comments with trailing dashes
(`// --- section ------`). Prefer a three-line block comment:

```cpp
//
// section
//
```

### Error Handling

Use `util::expected<T, E>` (similar to std::expected) for fallible operations. This is defined in `src/util/expected.h`.

### Parsing

The codebase has a custom lexer in `src/util/lex.h/cpp` for tokenizing inputs.
The shared parsing scaffolding — the `parse_error` type, the `PARSE` macro, and
the `parse_string` primitive — lives in `src/util/parse.h/cpp` (namespace `util`)
so that it can be reused by any module.
Parse functions return `util::expected<T, util::parse_error>`. The `uenv` parsers
are in `src/uenv/parse.h/cpp`; the OCI client has its own parsers in
`src/oci/parse.h/cpp` (see "Self-contained `src/oci`" below).
All inputs are parsed instead of using regex or simple string processing.
When we have to parse a new input type:
1. update the lexer (if needed)
2. add a `parse` function in the relevant `parse.h`/`parse.cpp`
3. write unit tests in the matching `test/unit/*.cpp`
4. run the unit tests, and repeat the process until they pass.

### Self-contained `src/oci`

The `src/oci/` module is a native OCI registry client written to replace the
external `oras` binary. It is intended to be reusable and independently testable,
so it must depend **only** on `src/util/` and external libraries (fmt,
nlohmann_json, libcurl, spdlog, zlib).

`src/oci/` must **not** include or depend on `src/uenv/`, `src/site/`, or
`src/cli/`. Code that both `uenv` and `oci` need (for example the lexer and the
parsing scaffolding) lives in `src/util/` precisely so that `oci` never has to
reach up into `uenv`:

- `src/util/lex.*` — the shared tokenizer.
- `src/util/parse.*` — `util::parse_error`, the `PARSE` macro, and
  `util::parse_string`.

Parsing in `src/oci` follows the same lexer-based idiom as the rest of the
codebase; JSON documents are parsed with nlohmann_json, while
`src/oci/parse.*` handles the character-level string types (digests, references,
URLs, the bearer challenge).

Enforcement: this command must return nothing.

```bash
grep -rn '#include <\(uenv\|site\|cli\)/' src/oci/
```

### The `argparse` command line parser

`src/argparse/` is the command line parser used by `uenv` and both
`squashfs-mount` variants (it replaced CLI11). `src/argparse/argparse.h`
documents the grammar.

Each command has an **arguments type**: a plain struct whose fields hold what
was given on the command line. A `command_builder<T>` binds each option and
positional to a field of `T` by pointer-to-member (`&run_args::view`), and
sets the command's action, which receives a filled-in `const T&`. The builder
produces an untyped `argparse::command`, added to its parent with
`add_subcommand(...)`. The root is wrapped in an `argparse::program<Globals>`,
where `Globals` is the root's arguments type (uenv's `global_args`).

Nothing is bound by reference and nothing is mutated as a side effect of
parsing:

- `argparse::parse()` only reads the tree: it classifies every word and
  returns a `parse_result` (a trace of which word went where, the parser state
  after the last word, and every error), and does not stop at the first
  error. This is what lets tab completion run the exact parser used for real
  invocations on an incomplete command line.
- `program::parse()` turns an error-free result into an `invocation`: new
  values of the root's arguments type (the global options) and of the
  selected command's, copied from their defaults and filled in, the latter
  bound to the command's action. `main()` reads `globals()`, loads the
  configuration, and then calls `run()`, which runs the action.

Each command's `detail::model<T>` holds its typed setters and action behind a
small virtual interface; the action, bound to the filled-in values, is
returned as a `std::function<int()>`. `program<Globals>` keeps a typed pointer
to the root's model, which is how `globals()` is typed without a cast.
Because only the builder and the action ever see `T`, each command's
arguments struct, implementation and footer are private to its `.cpp` file,
and its header declares a single function, e.g.
`argparse::command run_command(const global_settings&)`.

`command::validate()` checks that a tree is well formed, including that every
option or positional that takes a value declares how it is completed
(`.complete(...)`); `uenv` runs it at start-up, so a malformed tree fails every
test.

Like `src/oci`, it depends only on `src/util/` and fmt. Enforcement: this
command must return nothing.

```bash
grep -rn '#include <\(uenv\|site\|cli\|oci\)/' src/argparse/
```

Behaviour that differs from CLI11, on purpose:

- once a `rest` positional (the command in `uenv run <uenv> <command...>`) has
  its first word, every later word belongs to it, options included;
- flags never take a value (`--json=false` is an error), and `--opt=` gives
  an empty value instead of consuming the next word;
- `-h/--help` takes precedence over every other error;
- a word is only matched against the subcommands of the current command:
  CLI11 would also jump to a parent's or sibling's subcommand
  (`uenv image find ls` ran `uenv image ls`);
- `-5` is an unknown option, not a positional;
- `--color`/`--no-color` is one negatable flag: the last one given wins
  (CLI11 applied them in the order they were defined).

### Tab completion

uenv completes its own command lines, with the same parser and command tree
as a real invocation. The shell scripts contain no knowledge of the CLI: on
every TAB they run

```
uenv __complete --cword=N -- <the words on the command line, as typed>
```

and give the shell what it prints: one `value<TAB>description` line per
candidate (the value replaces the whole word at the cursor), then a line of
directives, `:` followed by any of `nospace`, `filenames` and
`command-offset=K` (see `src/cli/complete.h`). `__complete` is not in the
command tree: `main()` hands over to `complete_main()` before parsing. Try it
directly when debugging, e.g.
`uenv __complete --cword=3 -- uenv start --view= prgenv-gnu/24.11:v1`.

The work is split in three layers:

1. `argparse::complete()` (`src/argparse/complete.{h,cpp}`) works out what
   the word at the cursor is: a subcommand or option name, the value of an
   option (`--view x`, `--view=x`, `-vx`), or a positional, by applying the
   parser's word rules to the words before it. It offers subcommand and
   option names and choices, and returns the parse of the whole line
   (including words after the cursor) as context.
2. `src/uenv/complete.{h,cpp}` are pure functions that complete labels, uenv
   lists (commas, squashfs files, `:mount`), views, systems, repos and paths
   from data that has already been loaded. They are unit tested.
3. `src/cli/complete.cpp` dispatches on the argument's `completion` kind, and
   for `custom("tag")` on the tag (`uenv_list`, `view_list`, `local_label`,
   ...). It loads the configuration and opens the repositories lazily, with
   `--repo`/`--system` taken from the command line (`program::globals()` on
   the context).

Rules for anything on the completion path:

- **Silent.** Its output lands in the middle of the user's command line.
  `complete_main()` points stderr at `/dev/null` and turns logging off, and
  nothing may print to stdout except the candidates. Every test in
  `completion.bats` checks that stderr is empty.
- **Read-only.** It must not create or modify files: the configuration is
  loaded with `user_config_mode::read_only`, so a missing user config file is
  not created, and repositories are opened read-only. The one exception is
  the registry listing cache, see below: completion touches a refresh stamp
  and starts a detached process that rewrites a listing.
- **Fast and local.** Nothing is read until a candidate needs it (a
  subcommand name reads no configuration), and there is no network access.
  Views are read from the `meta/env.json` next to an image, never by
  extracting a squashfs file. Registry labels come from a cache, see below.
- **User text never reaches SQL.** `repository::query` formats its SQL
  without escaping; labels are completed by filtering every record of the
  repository in C++.

Registry labels are completed from a cache of the listing service, never
from the network (a request per TAB would be slow, can hang, and would load a
site-specific service). `site::registry_listing()` saves every listing it
fetches, so any `uenv image find/pull/copy/push/delete` fills it, to
`$XDG_CACHE_HOME/uenv/listing/<hash of listing URL>/<namespace>.json`
(`site::listing_cache_dir()`); a listing older than
`site::listing_cache_max_age` is ignored. With nothing cached, only namespace
names are offered, and the first TAB starts the fetch (see below). The tag of
the argument decides how namespaces are handled:

- `registry_label` (`image pull`, `image find`): labels in the configured
  default namespace, and `ns::` once something has been typed;
- `registry_nslabel` (`image delete`, the source of `image copy`): `ns::`
  first, then the labels in that namespace;
- `registry_dest` (the destination of `image copy` and `image push`): `ns::`,
  then `ns::name/version:` of the source, leaving the tag to the user, then
  the source's `@system%uarch` after an `@`.

The namespaces offered are `site::registry_namespaces`, the default namespace
and any namespace in the cache. Tests that run `image` commands against a
listing service set `XDG_CACHE_HOME` so that they never write to the user's
cache.

#### Refreshing the listing cache (stale-while-revalidate)

When completion reads a listing that is missing, invalid, or older than
`site::listing_refresh_interval` (60 s), it answers from what is cached and
starts a refresh for the next TAB: `site::claim_listing_refresh()` decides,
and `util::spawn_detached()` (`src/util/detach.h`) runs
`site::refresh_registry_listing()` in a detached grandchild. A namespace with
no usable listing is only fetched if it is one of the offered namespaces, so
that typing `typo::` sends no request.

The requirement that shapes this: **a TAB must never hang, and no state of the
cache may ever need to be deleted by hand** (the failure mode of the Tcl
modules cache). The rules that guarantee it, which must not be "simplified"
away:

- **Completion never waits.** It reads at most one listing per namespace,
  through `read_cache_file()`: a regular file only (a FIFO would block the
  reader) of at most `site::listing_cache_max_size` bytes. It never waits on
  the network, a lock, or the refresh.
- **No locks.** A refresh is claimed by touching `.<namespace>.refresh`, and
  not again until the stamp is an interval old, whether or not it succeeded:
  at most one request per namespace per minute, also while the service is
  down. A race only costs a second request. Never replace this with `flock`
  or a pid file: a lock left by a killed process is exactly what makes a
  cache need repairing by hand.
- **Every state of the cache is used or replaced.** Listings are written to a
  temporary file and renamed into place, and only after they parse; a listing
  that is invalid, too large or not a regular file is treated as missing and
  refreshed, and a directory in its place is removed. A stamp that is not a
  regular file is replaced. A time further than the interval in the future
  (a wrong clock) is not recent (`modified_within()`), so that it can't
  suppress refreshes. Temporary files older than an hour are removed by the
  next refresh. If the stamp can't be written (a read-only cache), no refresh
  is started, so a broken cache can't cause a fork per TAB.
- **The refresh holds nothing of the caller's.** `spawn_detached` forks twice
  and calls `setsid`, points stdin/stdout/stderr at `/dev/null` and closes
  every other descriptor. The shell scripts read `uenv __complete` with
  `$(...)`, which waits for EOF on stdout: a refresh that kept stdout would
  make every TAB wait for the network. `util::curl::get` gives up after 5 s,
  and `alarm()` kills the refresh after `listing_refresh_limit` (15 s)
  whatever it is doing. It runs in-process after `fork`, which is safe because
  the completion process is single-threaded: keep it so.

`completion.bats` checks each of these: a broken cache in every state, a
listing service that accepts connections and never answers, and a read-only
cache directory, each with a time limit on the TAB.

The shell scripts (`src/cli/completion/uenv.bash`, `src/cli/completion/_uenv`)
are installed with `install_data`, and compiled into the binary (meson reads
them into `completion_scripts.h`), so that `uenv completion bash|zsh` prints
them. The bash script does not depend on bash-completion. It has to put back
together the words that bash splits at `COMP_WORDBREAKS` (`=`, `:` and `@`
occur in uenv arguments), and give readline only the part of each candidate
after the last break character; `@` is kept in the word by bash, `=` and `:`
are not.

### Environment Variables

Use `envvars::state` to access environment variables (from `src/util/envvars.h`). Available as `settings.calling_environment` in most CLI commands.

The tool follows the philosophy of not reading environment variables directly using `getenv`, instead we grab a read only copy of the environment at startup, stored in `settings.calling_environment`.
When calling `exec` to run a new command with a modified environment, we copy this initial state, modify it, then pass the modified copy to `exec`.

### Logging

Use spdlog for logging. Set verbosity via `settings.verbose`. Format output using fmt library.

### File System Operations

Prefer using functions from `src/util/fs.h` which provide expected-based error handling over raw std::filesystem operations.

### Adding CLI Commands

1. Create header/source in `src/cli/` (e.g., `foo.h`, `foo.cpp`)
2. Implement command function returning `int` (exit code)
3. Add source to `cli_src` array in `meson.build`
4. In `foo.cpp`, put the command's arguments struct and its implementation in
   an anonymous namespace, and add
   `argparse::command foo_command(const global_settings& settings)`, the only
   declaration in `foo.h`. It builds the command with a
   `argparse::command_builder<foo_args>`: bind options and positionals to
   fields (`&foo_args::field`), give every one that takes a value a
   `.complete(...)`, set the action
   (`cmd.action([&settings](const foo_args& args) { return foo(args, settings); })`)
   and `return std::move(cmd).build();`. A new `completion::custom("tag")`
   needs a provider in `complete_custom()` (`src/cli/complete.cpp`); tags
   without one complete nothing.
5. Add it to its parent with `add_subcommand(...)`: top-level commands in
   `make_cli()` (`src/cli/cli.cpp`), `uenv image ...` commands in
   `image_command()`
6. Add integration tests in `test/integration/cli.bats`, and completion
   tests for new kinds of argument in `test/integration/completion.bats`
7. Add unit tests for any new library functions in `test/unit/`

### Testing New Features

- Add unit tests in `test/unit/` for library functions
- Add BATS integration tests in `test/integration/` for CLI behavior
- BATS tests use test data generated in `test/data/` and setup scripts in `test/setup/`
