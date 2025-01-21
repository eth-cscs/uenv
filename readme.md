# UENV 2

A rewrite of uenv in C++:
* deployed as static binaries
* no longer supports modification of the environment in the calling shell
* bring the CLI and Slurm plugin under one roof, with a common library

Not just a rewrite - new features!
* `uenv image build $recipe_path arbor/10.1@todi%gh200` - automatically dispatch a build job to build your uenv and deploy it on JFrog.
* `uenv image add arbor/10.1:v2@todi%gh200 $SCRATCH/myimages/arbor.squashfs` add a squashfs file to your local repository and give it a label
* `uenv image rm prgenv-gnu/24.11:rc4` remove a uenv from your local repository
* `uenv image find @eiger` find all uenv on eigher
* `uenv image find @'*'%gh200` show all uenv built for `gh200` on all clusters
* `uenv start ./store.squashfs --view=develop` now works - uenv can now "peek inside" squashfs images to read meta data
* **supports bash, zsh, fish, tcsh**, and no more bash wrappers polluting your history.
* and more...

Some old features are gone:
* `uenv view` and `uenv modules` have been removed - the tool can no longer modify the environment of the calling shell
    * all views must be loaded with the `--view` flag on `uenv run` and `uenv start`
    * or using the `--view` flag of `srun` and `sbatch`.

## Alps quickstart

To take uenv2 for a test drive on alps,

```
git clone https://github.com/eth-cscs/uenv2.git

cd uenv2

# this script will build uenv2, and install it in $HOME/.local
./install-alps-local.sh
```

(very rough) documentation is starting to form over here on the [KB proposal](https://bcumming.github.io/kb-poc/build-install/uenv/).

## building

The software uses meson wrap to bring its own dependencies, all of which are built as static libraries.

To build you only need
* meson >= 1.5
* ninja
* g++ that supports C++20 (we test g++12 regularly)

On your laptop these requirements can be met using your package manager of choice.

On an Alps vCluster, we want to use the system compiler "as is" without using a uenv or modules. The `g++` requirement is met by the `g++-12` compiler, that is installed on the vClusters as part of the boot image. The easiest way to set up meson and ninja is to pip install them to create an isolated build environment.

On Alps the version of Python3 is ancient, so it can't support the required meson version.
The `prgenv-gnu/24.11` uenv provides all of the tools required.

```
alias e="uenv run prgenv-gnu/24.11:v1 --view=default --"
CXX=g++-12 e meson setup -Dtests=enabled build
e meson compile -Cbuild
```

* use the system version of gcc-12 in order to create a statically linked binary that can run outside the uenv
* the integraton tests can't be run through the alias (or in the uenv), because it isn't possible to use `uenv run` or `uenv start` inside a uenv.

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

