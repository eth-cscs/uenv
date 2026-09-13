# Opening squashfs images as the caller, not as root

Fix for finding #2 of the 2026-09-12 security audit.

## The issue

Both privileged mount paths of the `kernel` backend opened the user-named
squashfs image with euid 0, and nothing anywhere in the tree checked whether the
invoking user was allowed to read it.

- **Setuid helper** (`src/squashfs-mount/squashfs-mount-kernel.cpp`, installed
  mode 4755 root:root) took `getuid()` only to restore it later. Argument
  parsing, mount-list validation and the image open all ran with euid 0; the
  drop to the calling user happened *after* the mount.
- **Slurm SPANK hook** (`impl::init_post_opt_remote`,
  `src/slurm/plugin_kernel.cpp`) runs inside slurmstepd as full root, before
  Slurm drops privileges. Its only credential change was `setegid(job_gid)`, an
  NFS root_squash workaround — the effective uid stayed 0 throughout. The
  comment above the validation call stated the missing check as a requirement
  ("check that the squashfs files exist and can be read by the user"); it was
  never implemented.

The result was a complete DAC bypass for reading squashfs images — file modes
*and* directory traversal, since every path operation ran as root:

```
squashfs-mount --sqfs=/home/victim/private.squashfs:/tmp/m -- tar cf - /tmp/m
srun --uenv=/home/victim/private.sqfs:/user-environment ls /user-environment
```

`UENV_MOUNT_LIST` reaches the root hook straight from the submitting user's
environment, so the local-context validation that ran as the user was not a
boundary — setting the variable directly bypassed it, and the Slurm path also
had a TOCTOU window, because the submit-side stat and the compute-node open are
separated in time and space.

Secondary effects: the error strings formed a root-privileged
existence/type/first-4-bytes oracle for arbitrary paths, since
`make_mount_pair`'s `weakly_canonical` / `is_regular_file` / `file_size` /
magic-read sequence also executed as root.

Scope: `kernel` backend only. The `fuse` backend is unprivileged throughout and
was never affected.

## The fix

**The image is opened with the calling user's credentials; only the loop-device
and `mount(2)` syscalls run as root.**

`LOOP_CONFIGURE` binds a file descriptor rather than a path, and DAC is checked
at `open(2)` only. So the open is moved ahead of the privilege elevation and the
descriptor is carried forward to the mount. The mount then inherits exactly the
caller's authority over the image, and because the path is never re-opened there
is no residual TOCTOU.

### Shared code — `src/uenv/mount_kernel.{h,cpp}`

- New `uenv::opened_image` (descriptor + canonical path + mount point) and
  `uenv::open_images()`, which opens each image `O_RDONLY|O_CLOEXEC|O_NOFOLLOW`
  with whatever credentials are currently in effect.
- `open_images()` **enforces** its precondition instead of documenting it: it
  refuses to run when the effective user is root and the real user is not. That
  contract had already been silently violated at both call sites, so it is a
  runtime check rather than a comment.
- `attach_loop_device()` takes the open descriptor plus a display name; the
  `fstat`/`S_ISREG` and `hsqs` magic checks stay, and are now the authoritative
  validation of a caller-supplied fd.
- `do_mount()` takes the opened images.
- New `util::unique_fd` (`src/util/unique_fd.h`), because the descriptors now
  outlive the call that opened them and travel through several error exits.
- New `src/util/privilege.{h,cpp}`: every uid/gid change and its read-back
  verification (`ids`, `user_ids`, `process_ids`, `current_ids`, `verify_ids`,
  `set_effective_uid`, `become_root`, `drop_privileges`, `run_as_user`).
  `mount_kernel.cpp` keeps only `unshare_mount_namespace()`, shared by the
  helper and the Slurm plugin (replacing `src/slurm/mount_slurm.{h,cpp}`).

### Setuid helper

`util::set_effective_uid()` to the real uid at the top of `main()`, so CLI
parsing, `parse_and_validate_mounts()` and `open_images()` all run as the real
user — which also closes the root oracle described above.

Privilege is reclaimed in `util::become_root()`, before
`uenv::unshare_mount_namespace()`. This needs two steps and the order is forced: dropping euid from 0
clears the effective capability set, so `unshare(CLONE_NEWNS)` would fail
`EPERM`; and an unprivileged process may set its *effective* uid from the
saved-set-uid but may **not** set its *real* uid from it, so `seteuid(0)` has to
precede `setreuid(0, 0)`.

On gids: the binary is setuid but not setgid, so the egid and supplementary
groups are already the caller's — which is both what makes the access check
complete and what the exec'd command must keep. No `setegid`/`setgroups` is
added, and a startup check refuses to run if the binary is ever installed setgid.
`util::drop_privileges()` takes the caller's real `util::ids` and drops the
gid before the uid, both with `setres*id` and both readback-verified: the
saved-set-gid survives `execve`, and `PR_SET_NO_NEW_PRIVS` does not prevent
regaining a saved id.

A clear diagnostic replaces the previous cryptic failure when the helper is not
installed setuid root and is asked to mount.

### Slurm plugin

`S_JOB_UID`, `S_JOB_GID` and `S_JOB_SUPPLEMENTARY_GIDS` are fetched and assumed
together. All three are needed: the uid alone leaves root's supplementary groups
in the credential set (too permissive), while omitting the job's own groups
refuses images the user reaches through a project group (too restrictive, and a
common case on Alps). If Slurm cannot supply the supplementary list the code
falls back to the primary gid alone — never to leaving root's groups in place.

The credential change happens via new `util::run_as_user()`
(`src/util/privilege.{h,cpp}`), which does the drop **on a dedicated thread using
raw syscalls**. Credentials are per-task on Linux; it is glibc that broadcasts
`setuid`/`setgid` to every thread. Confining the change to one thread leaves
slurmstepd itself root, which matters because this hook runs before Slurm drops
privileges, with the step's message thread already serving RPCs, and Slurm
serialises its own equivalent sequence behind `auth_setuid_lock()` — a lock a
SPANK plugin cannot take. The drop is one-way, so there is no restore path to
get wrong.

The `setegid(job_gid)` root_squash workaround is removed: opening as the real
job user subsumes and improves on it.

### Two latent bugs this made reachable — `src/uenv/mount.cpp`

- `std::filesystem::file_size` used the throwing overload. The setuid helper has
  no handler, so that was `std::terminate` in a setuid binary. Now uses the
  `error_code` overload.
- `is_regular_file` returns false on `EACCES`, and the code reported "is not a
  regular file". That branch was nearly unreachable as root; it is now the
  ordinary path for an unreadable image, so the diagnosis had to be accurate.
  Permission failures are now reported as such, including the `errno` from the
  open.
- Also fixed: the sqfs canonicalisation error named `d.mount_path` instead of
  `d.sqfs_path`.

## Behaviour changes

1. **An image that only root can read, or one inside a directory the user cannot
   traverse, is now refused.** Sites should check the modes and ancestor
   traversal bits of deployed image stores before upgrading.
2. An image readable through one of the user's *supplementary* groups now works
   under Slurm without submitting the job with `--gid=<group>`.
3. Mode-600 images on a root_squash NFS filesystem now work, since the image is
   no longer opened as root. This supersedes the workaround added in #135.
4. All images are opened up front rather than one at a time immediately before
   each mount, so an image file that only becomes reachable *inside* a
   previously-mounted image would no longer work. Nothing tested or documented
   relies on that: `recursive mount` nests mount points while using the same
   host-filesystem image, and `mount.cpp` only ever claimed nested mount points
   are supported.

## Verification

Both privileged paths were shown to be exploitable before the change and closed
after it, using the same command each time.

**Setuid helper**, against a staged setuid install, with a root-owned mode-600
image inside a root-owned mode-700 directory:

```
# before
$ squashfs-mount --sqfs=/tmp/vulncheck/image.squashfs:/user-environment -- .../app
hello app                                          # exit 0

# after
error: invalid squashfs ... (Permission denied)     # exit 1
```

**Slurm plugin**, against a live cluster, with a root-owned mode-600 image in a
directory anyone can traverse (so the submit-side checks, which only stat the
file, pass and the description reaches the root hook):

```
# before
$ srun --uenv=/.../rootowned/image.squashfs:/user-environment ls /user-environment
env
meta
modules                                            # exit 0

# after
srun: error: task 0 launch failed: Unknown negative error number   # exit 1
```

Test results:

- **unit**: 2355 assertions, 238 cases, all pass. New coverage for `unique_fd`,
  `run_as_user` and `open_images` (unreadable image, untraversable directory,
  absent image, descriptor flags, order preservation). The cases root would
  bypass skip when the suite is run as root.
- Run as root, the confinement test confirms the work saw uid 65534 while the
  process stayed euid 0 — the guarantee slurmstepd depends on.
- **slurm.bats**: 21/21 against a live Slurm controller and daemon, including
  the four new cases. The group-readable case confirms
  `S_JOB_SUPPLEMENTARY_GIDS` works end to end: an image reachable only through
  a *secondary* group mounts without the job passing `--gid`.
- **squashfs-mount.bats**: 19/19 against a staged setuid install, including the
  new refusal, oracle and gid cases, the group-readable positive control, and
  the pre-existing `recursive mount`.
- **cli.bats**: 15/15. **registry.bats**: 2/2.
- **FUSE build**: configures and compiles clean; 232 unit cases pass, with the
  kernel-only tests correctly excluded.

### Two things the Slurm testing corrected

**`UENV_MOUNT_LIST` is not a way in, so it is not what the tests should use.**
The first version of these tests set the variable directly. With no uenv
requested and none loaded, the local context *unsets* it before the step
launches, so those tests passed trivially without ever reaching the hook. The
real carrier is `--uenv=<file>:<mount>`: the submit-side checks
(`fs::exists`/`fs::is_regular_file`) only stat the file, so an image the user
can see but not read passes them and arrives at the root hook — which is where
it now gets refused.

**Root-owned fixtures broke the next run's `setup()`.** A root-owned file in the
scratch directory cannot be removed by the test user, so an interrupted run left
it behind and `rm -rf $TMP` then failed in `setup()` — failing whichever test ran
first, rather than reporting the real problem. Both suites now use
`reset_scratch_dir`, which falls back to `sudo -n` for exactly this case, and
both clean their root-owned fixtures in `teardown`.

### Still worth confirming on a real deployment

A mode-600 user-owned image on a root_squash NFS filesystem should now mount
under `srun`, where it could not before. That could not be tested here: it needs
a root_squash filesystem.

## Not fixed here

This closes the image side only. `do_mount` still resolves the mount *target* as
root, following symlinks, so for a nested mount (one whose target
`validate_mount_list` skips because it is lexically inside an earlier mount) a
root-privileged existence/type oracle survives. That is pre-existing, low
severity — private mount namespace, the user's own image — and is audit finding
#4, whose proper fix is `openat2(RESOLVE_NO_SYMLINKS)` plus mounting onto
`/proc/self/fd/N`.

The early euid drop does not harden the helper against memory-safety bugs in
CLI11 or fmt: the saved-set-uid stays 0, with a full permitted capability set,
until the final drop.

When the Slurm hook refuses an image, the user sees only `srun: error: task 0
launch failed: Unknown negative error number`; the reason goes to the
slurmstepd log. That is pre-existing behaviour for every error this hook
reports — `init_post_opt_remote` hardcodes `init_log(spdlog::level::off)`, so
`UENV_LOG_LEVEL` reaches the local and allocator contexts but not this one — but
the refusal added here is a new reason for users to hit it, on images that used
to work. Making the remote context honour `UENV_LOG_LEVEL` would be a small,
separate change.
