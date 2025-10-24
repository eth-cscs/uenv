# Integration Tests

Integration test for the CLI and SLURM plugin, using the [bats](https://github.com/bats-core/bats-core) testing framework.

## getting started

The best way to run the tests is to configure the meson build with tests enabled:

```
meson setup .. -Dtests=enabled
```

This will generate the test inputs in the `test` subdirectory of the build path, and install the test runners

## running the tests

There are three separate sets of tests.

- `slurm.bats`: test the slurm plugin
    - ensure that you have built the plugin and configured slurm in your environment to use the plugin.
- `cli.bats`: test the uenv cli
    - ensure that you have built the cli and that it is in your `PATH`.
- `squashfs-mount.bats`: test the squashfs-mount cli tool
    - the tool is setuid, so an additional installation step is required (see below)

To run the respective tests, build with `-Dtests=enabled`, then in the build path:
```console
./test/bats ./test/slurm.bats
./test/bats ./test/cli.bats
```

## testing squashfs-mount

```bash
# create a staging path where we will install 
export STAGING_PATH=$PWD/staging
meson setup .. -Dtests=enabled -Dsquashfs_mount=true --prefix=$STAGING_PATH
# build as user
meson compile
# install in the staging path as root (required to make squashfs-mount setuid)
sudo meson install --no-rebuild --skip-subprojects

# run the tests
# the test framework will detect the STAGING_PATH variable and use the uenv and squashfs-mount
# executables installed therein.

# test the cli with the version squashfs-mount installed in staging
./test/bats ./test/cli.bats
# test squashfs-mount
./test/bats ./test/squashfs-mount.bats
```
