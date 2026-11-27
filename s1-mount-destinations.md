# S1: no constraint on where a uenv may be mounted

Status: open. Out of scope for the `tmpfs-bind-mount` PR. First reported as
finding 4 of the 2026-09-12 security audit, and again as S1 in the review of
`src/uenv/mount.cpp`.

## The issue

`uenv::make_mount_plan()` accepts any existing directory as a mount
destination. The only check is that the directory exists. A user can therefore
mount a squashfs image that they built over any directory, for example `/etc`
or `/usr/lib64`.

Who is affected:

| Site | Affected | Why |
|---|---|---|
| Kernel backend, Slurm plugin | yes | mounts as root in the mount namespace of `slurmstepd`, which the tasks inherit |
| Kernel backend, setuid helper | no | private namespace; privilege is dropped and `PR_SET_NO_NEW_PRIVS` is set before the command is exec'd |
| Fuse backend | no | unprivileged throughout, and sets `PR_SET_NO_NEW_PRIVS` |

## Two routes to root in the Slurm plugin

Neither route has been exploited in a test. Both follow from reading the code.

### Route 1: setuid programs in the job

The kernel plugin does not set `PR_SET_NO_NEW_PRIVS` on the tasks.

1. The user submits a job with `--uenv=img.squashfs:/etc`. The image contains
   a `sudoers`, `passwd` or `pam.d` that the user wrote.
2. The job runs `sudo` or `su`. The setuid program reads its policy from the
   image and grants root.

This is the more direct route. The review of `mount.cpp` did not list it.

### Route 2: `slurmstepd` itself

`slurmstepd` is root, and continues to run in the mount namespace after the
hook returns. Anything that it reads or loads after the mount comes from the
image. The same applies to the privileged hooks of other SPANK plugins.
Candidates are the NSS modules that `/etc/nsswitch.conf` selects, and shared
libraries below a directory that was mounted over.

Whether a specific path is exploitable has not been verified.

## Mitigations

Each mitigation closes one route. Both are needed.

### A. `PR_SET_NO_NEW_PRIVS` on the tasks (closes route 1)

After the flag is set, `execve` can not grant privilege: setuid bits, setgid
bits and file capabilities are ignored. The flag can not be cleared and all
descendants inherit it. It does not remove privilege that a process already
has, so it does nothing for route 2.

Where: `slurm_spank_task_init`, which runs in the process of each task after
the fork and before the exec. Setting it in `slurm_spank_init_post_opt` marks
one thread of `slurmstepd`, and reaches the tasks only if they are forked from
that thread. That has not been checked.

When: only if the plugin mounted something. Jobs without `--uenv` are then
unchanged, and the behaviour matches `uenv run` and `uenv start`, where the
helper already sets the flag.

Consequences inside a job that used `--uenv`:

- `sudo`, `su` and `newgrp` stop working.
- `fusermount3` is setuid, so FUSE mounts that use it fail (sshfs, squashfuse
  outside a user namespace).
- Container runtimes that use a setuid starter, or `newuidmap`/`newgidmap`,
  fail: Sarus, Apptainer in setuid mode, rootless podman with subuid ranges.
  Check this against what users run on Alps before deploying.
- A nested `uenv run` or `uenv start` fails, because the kernel helper is
  setuid. This is already the case inside `uenv start`.
- Not affected: `srun` inside a batch script, unprivileged user namespaces,
  SSH sessions that are adopted into the extern step.

### B. An allowlist of destinations (closes route 2)

A list of permitted destination prefixes, set by the site and enforced in
`make_mount_plan()`.

**Where the list lives.** It should not be hard-coded. The site configuration,
`/etc/uenv/config.toml`, is the natural place, but uenv lets a user override
site settings in `$HOME/.config/uenv/config.toml`. Two ways to deal with that
were considered:

1. Mark some fields as not overridable by the user layer.
2. Never read the user configuration at the privileged sites.

Option 2 is already true: neither privileged site reads any configuration
today. Both `uenv::load_config()` calls in the plugin are in
`init_post_opt_local_allocator`, on the submit side, and the setuid helper
does not call it.

Recommendation: keep the list out of the layered configuration. Add a separate
loader that reads only `/etc/uenv/config.toml`, and only a `[mount]` table. It
does not go through `load_config()` or the merge. A `[mount]` table in a user
configuration is ignored with a warning, or rejected.

Reasons to prefer this over option 1:

- Option 1 is a rule in the merge code. It holds only while every reader goes
  through the merge, and while every new layer remembers the rule.
- The location of the user configuration comes from `HOME` and
  `XDG_CONFIG_HOME`. At both privileged sites, the user sets these. The system
  path has no input from the environment.
- `load_user_config()` creates the directory and the file when they are
  missing. Inside `slurmstepd` that is a write, as root, to a path that the
  user chose. `load_config()` must not run there.

Details of the loader:

- Open the file, `fstat` the descriptor, and require that root owns it and
  that it is not writable by group or others.
- Pass the policy to `make_mount_plan()` as an argument. `mount.cpp` then
  reads no files, and the unit tests can supply a policy.
- The check at the privileged sites is the one that counts. The submit side
  can run the same check to report the error early. The file must be present
  on the compute nodes, not only on the login nodes.
- The fuse backend does not need a policy.

**Alternative source.** The arguments of the plugin in `plugstack.conf` are
controlled by root and are not affected by the user configuration. They only
reach the plugin, so the setuid helper would still need the file in `/etc`.

**The allowlist is not sufficient alone.** Finding C2 of the review: a symlink
inside an outer image redirects a nested mount after the path was checked. The
kernel backend mounts by path. It must also resolve the target without
following symlinks, or refuse a destination inside another image when a policy
is active.

## Open decisions

1. What happens on the kernel backend when no policy is configured:
   - refuse to mount (recommended): existing deployments stop mounting on
     upgrade until the site adds the key, so the error must name it;
   - allow every destination: no change on upgrade, and route 2 stays open by
     default;
   - a default list from a meson option.
2. Whether mitigation A is acceptable on Alps, given the container runtimes
   that users run inside `--uenv` jobs.
3. How to handle nested destinations (C2) when a policy is active.

## Suggested order

1. Mitigation A. It is a few lines, and it closes the more direct route.
2. Mitigation B together with the fix for C2.
