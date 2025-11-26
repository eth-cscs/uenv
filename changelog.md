# Changelog

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
