# For building RPMs.

RPM packaging requires that it performs the `meson setup ...`, `meson compile ...`
and `meson install ...` itself, in a controlled environment. The script provided
here drives that process, after making suitable sacrifices to the RPM packaging gods.

**WARNING**: the tools here build the RPM based on the state of the source code at `HEAD`
in the git repository. If you have uncommitted changes, they will not be reflected
in the generated RPM.

**NOTE**: building RPMs is fiddly business - it isn't you, it is `rpmbuild`. Contact
Ben or Simon for help instead of trying to find good docs on RPMs (they don't exist).

## make-rpm.sh

A script that will generate both source and binary RPMs for the project.

It requires a destination path where the RPM build will occur, and should be run in this path.

```
./rpm-build.sh $HOME/rpm/uenv
```

## macros.meson

The spec file `slurm-uenv-mount.spec` uses macros like `%meson_setup`, which parameterise
calls to meson. Macros for meson are not usually available, so we provide a definition of
the macros in `macros.meson`. They have been modified from the ones provided in the
following RPM:

https://packages.altlinux.org/en/sisyphus/binary/rpm-macros-meson/noarch/2908824343041241330
