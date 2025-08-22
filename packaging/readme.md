# For building RPMs.

The binary rpm for uenv2 is built in a docker container using the same opensuse/leap version as the target cluster.

## Docker

1. Build the base container
    ```bash
    podman build -f sles15sp5/docker/Dockerfile . -t uenv2-rpmbuild
    ```

2. Build the binary rpm inside the container
    ```bash
    podman run -v $(pwd):/work:rw -w /work -it uenv2-rpmbuild:latest sh -c 'CXX=g++-12 CC=gcc-12 ./build.sh --ref=v8.1.0 --slurm-version=25.05.2'
    ```
    After a successful build the `uenv-<version>-<slurm_version>.<uarch>.rpm` will be copied to the current directory. 
    See `./build.sh -h` for more options.
    **NOTE** The rpm spec file and scripts will be used from the current branch, not from the release/gitref that is specified in `--ref`. In 2. the current directory is bind-mounted, standard out/error from the rpm build process are redirected to `stdout|err.log`.
    **NOTE** meson setup options are hardcoded in [macros/macros.meson](./macros/macros.meson).

### CSCS podman config

Before calling `podman build` make sure the following config file exists:
`$HOME/.config/containers/storage.conf`:
```conf
[storage]
  driver = "overlay"
  runroot = "/dev/shm/<username>/runroot"
  graphroot = "/dev/shm/<username>/root"

[storage.options.overlay]
  mount_program = "/usr/bin/fuse-overlayfs"
```

## Notes

RPM packaging requires that it performs the `meson setup ...`, `meson compile ...`
and `meson install ...` itself, in a controlled environment. The script provided
here drives that process, after making suitable sacrifices to the RPM packaging gods.

**WARNING**: the tools here build the RPM based on the state of the source code at `HEAD`
in the git repository. If you have uncommitted changes, they will not be reflected
in the generated RPM.

**NOTE**: building RPMs is fiddly business - it isn't you, it is `rpmbuild`. Contact
Ben or Simon for help instead of trying to find good docs on RPMs (they don't exist).


### macros.meson

The spec file `uenv.spec` uses macros like `%meson_setup`, which parameterise
calls to meson. Macros for meson are not usually available, so we provide a definition of
the macros in `macros.meson`. They have been modified from the ones provided in the
following RPM:

https://packages.altlinux.org/en/sisyphus/binary/rpm-macros-meson/noarch/2908824343041241330
