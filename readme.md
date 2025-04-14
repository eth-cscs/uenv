# UENV 2

Documentation for uenv2 is available on the [new cscs documentation](https://eth-cscs.github.io/cscs-docs/software/uenv/).

A rewrite of uenv in C++:
* deployed as static binaries
* no longer supports modification of the environment in the calling shell
* bring the CLI and Slurm plugin under one roof, with a common library

Some old features are gone:
* `uenv view` and `uenv modules` have been removed - the tool can no longer modify the environment of the calling shell
    * all views must be loaded with the `--view` flag on `uenv run` and `uenv start`
    * or using the `--view` flag of `srun` and `sbatch`.

## Alps quickstart

> Before running the build script, install [uv](https://docs.astral.sh/uv/getting-started/installation/), which is used to provide meson and ninja for the build.

To take uenv2 for a test drive on alps,

```
git clone https://github.com/eth-cscs/uenv2.git

cd uenv2

# this script will build uenv2, and install it in $HOME/.local/$(uname -m)/bin
./install-alps-local.sh
```

## building

The software uses meson wrap to bring its own dependencies, all of which are built as static libraries.

To build you only need
* meson >= 1.5
* ninja
* g++ that supports C++20 (we test g++12 regularly)

On your laptop these requirements can be met using your package manager of choice.

On an Alps vCluster, we want to use the system compiler "as is" without using a uenv or modules. The `g++` requirement is met by the `g++-12` compiler, that is installed on the vClusters as part of the boot image.

On Alps the version of Python3 is too ancient to support the required meson version.
Install [uv](https://docs.astral.sh/uv/getting-started/installation/) to use appropriate Python and meson versions.

```
alias e="uvx --with meson,ninja"
CXX=g++-12 e meson setup -Dtests=enabled build
e meson compile -Cbuild
```

* use the system version of gcc-12 in order to create a statically linked binary that can run outside the uenv

## testing

There are three sets of tests:

* `unit`: unit tests for the C++ library components (Catch2)
* `cli`: tests for the CLI interface (bats)
* `slurm`: tests for the Slurm plugin (bats)

To build tests, use the `-Dtests=enabled` flag to meson

```
meson setup -Dtests=enabled
```

The tests are installed in the `test` sub-directory of the build path:
```
cd build/test

# run the unit tests
./unit

# run the cli tests
./bats cli.bats
```

The tests can also be run using meson:
```
# run all the tests
meson test

# or run test suites separately
meson test cli
meson test unit
```

**NOTE**: the slurm integration tests require configuring Slurm to use the plugin, which requires root permissions.
For this reason, they can't be tested on Alps vClusters.

