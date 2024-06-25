Design document for handling of environment variables.

The uenv CLI and Slurm plugin need to modify environment variables, to implement some key features that automatically configure the runtime environment according to flags and configuration files provided when launching a job or starting a uenv.

This document outlines a single design for how this should be achieved for all use cases and both the CLI and Slurm plugin use case.

## Use Cases

### Activating views

Activating a view sets environment variables like `PATH`, `PKG_CONFIG_PATH`, etc.

### Configuration files

We want to support configruation files that specify a uenv, and how the environment should be configured, fo users.

This includes explicitly setting environment variables.

## Constraints

The main constraint is how Slurm executes jobs. The Slurm daemon starts a single process on each node, then forks into a separate process for each MPI rank on the node.
Each fork then runs the user-provided command using `execve`. At no point is a new shell started.

As a result, it is not possible to source a shell script in the environment, that might set environment variables.
Instead, the only way to modify the environment is to use the `spank_setenv` function, which updates a list of environment variables to be passed to `execve`.

Environment modification:
- create a new namespace and mount squashfs images.
- set environment variables using `spank_setenv`, `os.setenv`, etc.

## Design

The current design, a bash function wrapper that `exec`s commands returned by the `uenv-impl` app, supports modifying the environment of the calling shell.
This enables `uenv view x` to "load a view" in the running shell, by modifying environment variables.
Unfortunately, this approach is fragile and complicated.

A more robust design would drop the ability to modify the calling shell's environment.

There would no longer be separate `uenv-impl` ane `uenv-image` commands - these would be merged into a single `uenv` executable that would be called directly on the CLI (instead of indirectly calling them via a bash function).

Modifications to the environment would only be performed via calls to `uenv start` and `uenv run`, each of which starts a new shell.

```
uenv start --view=develop icon
|
|_fork__
        |
        |
        setenv("PATH", ...)
        setenv("LD_LIBRARY_PATH", ...)
        |
        execve(bash)
        |
        |_fork__
                |
                |
                bash
```

## Features

1. support for scalar and list variables.
2. three different modes for setting values
    - set: scalar and list variabls
    - unset: scalar and list variabls
    - prepend: list variables
    - append: list variables


Data structure

- `var.type`
    - `scalar`: a scalar value
    - `list`: a colon separated list, like `PATH`, `LD_LIBRARY_PATH`, etc.
- `var.operation` is one of
    - `prepend`: prepend to a list
    - `append`: prepend to a list
    - `set`: set the value to equal
    - `unset`: 
each variable has three values
- var.old
    - the value currently set in the environment, before modification
    - can be `null`
- var.value
    - the value provided by uenv
- var.new
    - the value set by uenv
    - this is apply(env.delta, env.old, env.operation)


class envVar:
