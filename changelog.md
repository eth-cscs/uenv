# Changelog

## working (since v8.1.0)

- #83 [fix] Turn some CLI flags into options, so that they can be set with or without `=`. e.g. `uenv --repo=$HOME/uenv` or `uenv --repo $HOME/uenv`.
- #87 [fix] Only use meta data path in adjacent to a uenv image if it contains an env.json file.
- #90 [fix] image push was not pushing the correct meta data path
