# UENV

Documentation for uenv is available on the [CSCS documentation](https://eth-cscs.github.io/cscs-docs/software/uenv/).

## Alps quickstart

> Before running the build script, install [uv](https://docs.astral.sh/uv/getting-started/installation/), which is used to provide meson and ninja for the build.

To take uenv for a test drive on alps,

```
git clone https://github.com/eth-cscs/uenv.git

cd uenv

# this script will build uenv, and install it in $HOME/.local/$(uname -m)/bin
./install-alps-local.sh
```

## Building

The software uses meson wrap to bring its own dependencies, all of which are built as static libraries.

To build you only need
* meson >= 1.5
* ninja
* g++ that supports C++20 (we test g++12 regularly)

On your laptop these requirements can be met using your package manager of choice.

On an Alps vCluster, we want to use the system compiler "as is" without using a uenv or modules. The `g++` requirement is met by the `g++-12` compiler, that is installed on the vClusters as part of the boot image.

There are 4 main components that can be enabled/disabled

* `-Dcli=true`: the command line tool
    * the `uenv` executable that is the main interface with uenv in the shell
    * default `true`
* `-Dslurm_plugin=true`: build the Slurm plugin
    * default `true`
* `-Dsquashfs_mount=true`: build the `squashfs-mount` CLI tool
    * default `disabled`
    * note that this can only be installed and tested on a system where you have root access
* `-Dtests=enabled`: enable tests
    * default `disabled`


```bash
cd uenv

# make a build directory
mkdir build
cd build

# configure and compile
meson setup -Dtests=enabled -Dsquashfs_mount=true
meson compile

# install: this will install in the default location (/usr/local)
sudo meson install --no-rebuild --skip-subprojects

# to install in a temporary stagin path
sudo meson install --destdir=$PWD/staging --no-rebuild --skip-subprojects
```

On Alps the version of Python3 is too ancient to support the required meson version.
Install [uv](https://docs.astral.sh/uv/getting-started/installation/) to use appropriate Python and meson versions.

The following will build the cli, and run tests, on an Alps system:
```bash
# from a build directory inside the uenv repository
alias e="uvx --with meson,ninja"
CXX=g++-12 e meson setup -Dtests=enabled ..
e meson compile

# run the tests
./test/bats ./test/cli.bats
./test/unit
```

## Testing

There are three sets of tests:

* `unit`: unit tests for the C++ library components (Catch2)
* `cli`: tests for the CLI interface (bats)
* `slurm`: tests for the Slurm plugin (bats)
* `squashfs-mount`: tests for the setuid squashfs-mount helper (bats)

To build tests, use the `-Dtests=enabled` flag to meson, and also enable the components to test

```bash
meson setup -Dtests=enabled -Dsquashfs_mount=true
```

The Slurm plugin and CLI are enabled by default, and their bats tests will be configured if `-Dtests=enabled` is set.

To test the `squashfs-mount` helper, enabled it when configuring the build:
```bash
meson setup -Dtests=enabled -Dsquashfs_mount=true
```

The tests are installed in the `test` sub-directory of the build path:
```bash
meson compile

# run the unit tests
./test/unit

# run the cli tests
./test/bats ./test/cli.bats

```

The tests can also be run using meson:
```
# run all the tests
meson test

# or run test suites separately
meson test cli
meson test unit
```

Running the `squashfs-mount` tests requires an additional step, because it has to be installed as [setuid](https://en.wikipedia.org/wiki/Setuid).

```bash
cd uenv

# make a build directory
mkdir build
cd build

# set up a staging path for installing the setuid version
export STAGE=$PWD/staging
export STAGING_PATH=$STAGE/usr/local

# configure and compile (enable squashfs-mount)
meson setup -Dtests=enabled -Dsquashfs_mount=true
meson compile

# install in the staging path path
# * --no-rebuild will not build - so the meson compile stage must be run first
# * this approach uses sudo _only_ for installing uenv for testing
sudo meson install --destdir=$STAGE --no-rebuild --skip-subprojects

# run the tests
# the STAGING_PATH env. var will be used by the tests to find the setuid version of squashfs-mount

# test cli with the squashfs-mount we just built
./test/bats ./test/cli.bats

# test squashfs-mount
./test/bats ./test/squashfs-mount.bats

# run the unit tests
./test/unit
```
**NOTE**: never build using sudo, i.e. do not `sudo meson compile` or `sudo ninja`. Instead build without root, then install in the staging path as root.
This avoids running any of the scripts called in the main build as root.

**NOTE**: the slurm integration tests require configuring Slurm to use the plugin, which requires root permissions.
For this reason, they can't be tested on Alps vClusters.

