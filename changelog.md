# Changelog

## 10.2.0

- #190 [improvement] `uenv image delete` deletes through the native OCI client instead of the JFrog Artifactory REST API; the `registry.artifactory_url` config key is removed (still accepted and ignored, so it can be dropped from deployed configs at leisure)
- #190 [improvement] `uenv image delete` resolves credentials exactly like `push` and `pull` (`--token`, then the uenv token store, then `~/.docker/config.json`), now that deletion authenticates against `registry.url`: `--token` is no longer required, and an unauthorised delete is reported by the registry instead of being refused locally
- #189 [fix] an empty `XDG_CONFIG_HOME` is treated as unset; one implementation of XDG directory lookup
- #188 [improvement] completion: single rule for expanding a leading `~`
- #187 [fix] tab completion and `main()` load the configuration with the same code; print the reason for an invalid `--repo`
- #186 [improvement] move bounded/atomic file helpers from the listing cache into `util/fs`
- #185 [improvement] `ready_fork`: add `fork_and_wait_ready` and `die_with_parent`, used by the FUSE mount daemons and supervisor
- #184 [fix] elastic telemetry is posted from a detached process that is reaped, holds no descriptors of srun and has a time limit
- #183 [improvement] shared `util::redirect_to_null` and `util::close_fds_from` replace four hand-written /dev/null redirections
- #182 [improvement] single helper for fetching registry listings in the CLI
- #181 [fix] print the reason when the registry listing can't be fetched in `image find/pull/push/copy/delete`
- #180 [fix] `util::run`: the forked child could return into the caller and continue as a second copy of uenv; report pipe and fork errors
- #179 [improvement] bash and zsh completion computed by uenv itself, with the same parser as the CLI
- #178 [feature] replace CLI11 with internal argument parsing library that facilitates completion
- #176 [security] `kernel` mounting opens squashfs images with the credentials of the user, instead of as root
- #175 [security] validate digests of manifests against what the registry prodides to protect against malicious registries
- #168 [security] replace libmount with direct loop-device ioctls in the kernel backend
- #172 [feature] add a rootless FUSE mounting backend: an unprivileged alternative to the setuid kernel backend.

## 10.1.0

- #174 [fix] generate explicit error when mounting two uenv with the same name
- #173 [improvement] consistent image hashes
- #171 [fix] use openssl sha256 implementation to improve download/upload speeds
- #166 [feature] replace the oras CLI tool with a native OCI registry client

## 10.0.2

- #163 [improvement] add label/squashfs information to uenv status output

## 10.0.1

- #162 [improvement] rename the `UENV_ARG` environment variable to `UENV_LABEL`
- #160 [fix] `uenv status` bug when no views are loaded, and only print active information about the loaded environment
- #159 [improvement] print repo priority in json output of `uenv repo status --json`

## 10.0.0

- #151 [fix] change error to warning when meta data is not attached to images in a registry
- #150 [fix] fix missing headers in gcc 16
- #149 [feature] generate RPMs in github actions
- #148 [feature] support advanced slurm workflows
- #147 [feature] support using '*' as cluster name in configuration files and --system flag
- #145 [feature] move nearly all cscs-specific logic into configuration files
- #144 [fix] fix latent bug parsing date strings
- #143 [feature] improved bash completion for uenv labels and files
- #142 [feature] use toml for configuration; support multiple repositories
- #136 [feature] support for default views

## 9.2.0

- #135 [fix] mount squasfs files on root squashed NFS mounts
- #133 [feature] improved error message for revoked registry tokens
- #131 [feature] remove --list flag in favor of a --format flag
- #129 [feature] `uenv image find` and `uenv image ls` do partial match on uenv names
- #127 [feature] find views, mount point, etc. information using `uenv image inspect` without opening a uenv.

## 9.1.2

- #125 extend `uenv image add` to support adding uenv that are already in a repo
- #126 fix bug looking up the default repository location on non-production systems at CSCS

## 9.1.1

- #120 rename cluster field in elastic logs to avoid name conflict
- #122 clean up `uenv status --format=views` output
- #124 restrict lustre striping to max 32 OST

## 9.1.0

- #116 [feature] add support for lustre striping and cleaning up missing images from repos
- #118 [feature] add `uenv repo migrate` feature

## 9.0.1

- #115 [fix]: fix bash function forwarding bug that broke the module command

## 9.0.0

- #83 [fix] Turn some CLI flags into options, so that they can be set with or without `=`. e.g. `uenv --repo=$HOME/uenv` or `uenv --repo $HOME/uenv`.
- #87 [fix] Only use meta data path in adjacent to a uenv image if it contains an env.json file.
- #90 [fix] `image push` was not pushing the correct meta data path.
- #91 [fix] Add `--json` option to `image ls` and `image find`.
- #93 [fix] a bug where the `--only-meta` flag was ignored on `image pull`.
- #96 [improvement] for unsquashfs to use a single thread when unpacking meta data.
- #97 [improvement] reimplement squashfs-mount in the main repository
- #99 [improvement] improve file name completion in bash.
- #104 [feature] elastic logging.
- #111 [feature] add --format flag to uenv status
- #112 [fix] add hints to error message when uenv is not found
