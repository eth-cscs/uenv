# Authenticating with a uenv registry

Most uenv commands need no credentials at all. Authentication is only involved
when uenv talks to the OCI registry (JFrog at CSCS), and only for images that
are not publicly readable, or for operations that write to the registry.

## Which commands need credentials

| command | needs credentials? |
| --- | --- |
| `uenv image ls` | no — it lists the local repository |
| `uenv image find` | no — the listing service is public |
| `uenv image pull` (public uenv) | no |
| `uenv image pull` (restricted uenv, e.g. licensed software) | yes |
| `uenv image push` | yes |
| `uenv image copy` | yes |
| `uenv image delete` | yes (`--token` is a required flag) |

`uenv image find` queries the uenv listing service, not the registry, so a
restricted uenv can be *found* without credentials but not *pulled*.

## What a credential is

A credential is a **username** and a **token** (the token is used as the
password). uenv never stores or asks for your interactive password: the token is
a personal access token (or a token issued for a licensed application) that you
place in a file.

A token file is a plain text file whose **first line** is the token, and nothing
else. It holds a secret, so it should be readable only by you:

```bash
chmod 600 <token-file>
```

uenv warns if a token file in its token store is readable by group or others.

## Where uenv looks for credentials

Credentials are resolved from the first source that has an entry, in this order.
Later sources are not consulted once an earlier one matches:

### 1. `--token` on the command line

```bash
uenv image pull --token=/opt/cscs/uenv/tokens/vasp6 vasp/6.4.2:v1
```

The argument is either

* a path to a token file, or
* a path to a **directory**, in which case `<directory>/TOKEN` is read.

At CSCS, tokens for licensed software are deployed as directories under
`/opt/cscs/uenv/tokens/`, which is why the example above names a directory.

If `--token` is given but the path does not exist, or cannot be read, the
command fails — uenv does not silently fall back to the other sources.

### 2. The uenv token store

```
$XDG_CONFIG_HOME/uenv/tokens/<registry-host>
```

or, when `XDG_CONFIG_HOME` is not set,

```
$HOME/.config/uenv/tokens/<registry-host>
```

`<registry-host>` is the host (and port, if the registry URL has one) of the
registry configured in `registry.url` — for the CSCS deployment that is
`jfrog.svc.cscs.ch`, so:

```bash
mkdir -p ~/.config/uenv/tokens
printf '%s\n' "$MY_TOKEN" > ~/.config/uenv/tokens/jfrog.svc.cscs.ch
chmod 600 ~/.config/uenv/tokens/jfrog.svc.cscs.ch
```

`uenv image delete` uses `registry.artifactory_url` instead, so its token is
looked up under that host.

Use this when you push or pull restricted uenv regularly and do not want to pass
`--token` every time.

### 3. `~/.docker/config.json`

If neither of the above matches, uenv reads the docker config, so a
`docker login` to the registry is enough:

```bash
docker login jfrog.svc.cscs.ch
```

uenv understands both forms that docker writes:

* an inline `auths["<host>"].auth` entry (base64 of `username:token`), and
* a credential helper (`credHelpers["<host>"]`, or the global `credsStore`),
  which is queried with `docker-credential-<helper> get`.

`$DOCKER_CONFIG/config.json` is used instead when `DOCKER_CONFIG` is set. A
missing or unreadable docker config is not an error — it just means no
credentials from this source.

### 4. Nothing found

uenv proceeds anonymously. That is correct and sufficient for pulling public
uenv.

## Where the username comes from

* `--username` if given;
* otherwise `$USER`, and failing that `$LOGNAME`.

Sources 1 and 2 supply only a token, so the username has to be filled in. The
docker config (source 3) carries the username with the token, and never needs
`--username`.

Batch jobs are fine: Slurm passes `$USER` through to the job, so a `--token` or
token-store push works the same in a batch step as in a login shell. The one
case left is a deliberately sanitised environment (`env -i`, some CI and cron
setups), where neither variable is set and uenv reports:

> a token was found, but uenv could not determine your username: pass it with
> `--username`.

Pass it explicitly there:

```bash
uenv image push --username=$USER --token=$HOME/.config/uenv/tokens/jfrog.svc.cscs.ch \
    ./store.squashfs prgenv-gnu/24.11:v3@daint%gh200
```

Note that under `sudo`, `$USER` is the target user, so `sudo uenv image push`
authenticates as that user — consistent with the token store and docker config
it also reads from that user's home directory.

## Interpreting errors

**401 — authentication required.** No credentials were found, or the registry
rejected the ones that were sent. Either you are pulling a restricted uenv
anonymously, or the token is wrong, expired, or is being paired with the wrong
username.

**403 — authenticated, but not permitted.** The credentials were accepted, but
the account is not allowed to do this. Typically: you have not been granted
access to a licensed application, or you are pushing to a namespace (for example
`deploy::`) that you may not write to. A different token will not help; access
has to be granted.

To see which source the credentials came from, run with `-vv`, which logs the
token file, token store entry or docker config that was used, or reports that
anonymous access is being used:

```bash
uenv -vv image pull vasp/6.4.2:v1
```

## Obtaining a token

<!-- TODO for the online docs: how users request access to licensed software,
     and how they generate a personal access token in JFrog. Not derivable from
     the source. -->
