A command line interface for interacting with user environments on Alps at CSCS.

## Getting uenv

uenv will be installed system wide for all users on some CSCS Alps systems. If it is not installed, you can install a local copy by cloning the repository and running the installation script:

```bash
git clone https://github.com/eth-cscs/uenv.git && ./uenv/install --local
```

For uenv to be available on the command line, an activation script has to be sourced, for which we recommend adding a line to your `~/.bashrc`.
The installation script will prompt you whether it should add the line, and provide a hint on how to update `.bashrc` if you choose to do it yourself.

!!! info
    When environments are loaded with, e.g. with `uenv start`, a new shell is started with the environment mounted at `/user-environment`.
    In order for the `uenv` commands to be available in the new shell, the activation script must be sourced when the shell starts -- which will be performed automatically if added to your `.bashrc` file.

!!! warning
    Only bash has been tested so far, though zsh should also work.
    Create a GitHub issue if there is a shell that you need to support.

## Using uenv

### Loading an environment

```bash
uenv start $SCRATCH/images/gromacs.sqfs
uenv start $SCRATCH/images/gromacs.sqfs:/user-environment
```

Will start a new shell with the environment `gromacs.sqfs` mounted at `/user-environment`.

```bash
uenv start $SCRATCH/images/gromacs.sqfs debugger.sqfs:/user-tools
```

Will start a new shell with the environment `gromacs.sqfs` mounted at `/user-environment`, and `debugger.sqfs` mounted at `/user-tools`

### Stopping an environment

To stop an environment, you can type `exit` or hit `ctrl-d` in your terminal, or issuing the following command:

```bash
uenv stop
```

!!! info
    The history of commands that were typed in the shell will be lost to the calling shell.

### Running a command in an environment

The `uenv run` command can be used to execute a single command in a shell with the desired environment loaded.
This is particularly useful for workflows that might execute steps with different environments.

Some examples:

```bash
uenv run $SCRATCH/images/gromacs.sqfs -- ls -ahl /user-environment
uenv run $SCRATCH/images/gromacs.sqfs:/user-environment $SCRATCH/images/tools.squashfs:/user-tools -- ls -ahl /user-environment /user-tools
```

Will start run a command in an environment with the , before returning to the calling shell.

### Status

To get the status of all loaded environments:

```bash
uenv status
```

