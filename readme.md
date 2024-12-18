# UENV 2

A rewrite of uenv in C++:
* deployed as static binaries
* no longer supports modification of the environment in the calling shell
* bring the CLI and Slurm plugin under one roof, with a common library

Not just a rewrite - new features!
* `uenv image build $recipe_path arbor/10.1@todi%gh200` - automatically dispatch a build job to build your uenv and deploy it on JFrog.
* `uenv image add $SCRATCH/myimages/arbor.squashfs arbor/10.1:v2@todi%gh200` add a squashfs file to your local repository and give it a label
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
* meson
* ninja
* g++ that supports C++17 (including the `std::filesystem` library implementation

On your laptop these requirements can be met using your package manager of choice.

On an Alps vCluster, we want to use the system compiler "as is" without using a uenv or modules. The `g++` requirement is met by the `g++-12` compiler, that is installed on the vClusters as part of the boot image. The easiest way to set up meson and ninja is to pip install them to create an isolated build environment.

```
python3 -m venv ./.env
source .env/bin/activate
pip install ninja meson

mkdir build
cd build
CC=gcc-12 CXX=g++-12 meson setup ..

ninja
```

## testing

**NOTE**: the tests require setup stages that are not straightforward to set up on Alps. A PR that fixes these issues is coming soon.

The C++ library has unit tests, that are built by default as the `unit` executable in the build path:

```bash
> ./unit
Randomness seeded to: 478418581
===============================================================================
All tests passed (137 assertions in 16 test cases)
```
