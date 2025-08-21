#!/bin/bash

usage="$(basename "$0") [-h] [-s,--skip-binary] [--slurm-version] dstdir

Helper script to create a source and binary rpm of the project.

Options:
-h,--help          show this help text
-s,--skip-bin      skip creation of binary package
--slurm-version    slurm version to specify in RPM dependency, defaults to 20.11.9
"

# Default argument
RPM_SLURM_VERSION=20.11.9

# Initialize our own variables
skip_bin=0

# A temporary variable to hold the output of `getopt`
TEMP=$(getopt -o s,h --long skip-bin,help,slurm-version: -- "$@")

# If getopt has reported an error, exit script with an error
if [ $? != 0 ]; then
  # echo 'Error parsing options' >&2
  echo "${usage}" >&2
  exit 1
fi

eval set -- "$TEMP"

# Now go through all the options
while true; do
  case "$1" in
  -s | --skip-bin)
    skip_bin=1
    shift
    ;;
  --slurm-version)
    shift
    RPM_SLURM_VERSION="$1"
    shift
    ;;
  -h | --help)
      shift
      echo "${usage}"
      exit 1
    ;;
  --)
    shift
    break
    ;;
  *)
    echo "Internal error! $1"
    exit 1
    ;;
  esac
done

# Remaining dstdir is in $1
dstdir=$(realpath $1)

# Check if the positional argument was provided
if [ -z "$dstdir" ]; then
  echo "${usage}" >&2
  exit 1
fi

# Print the parsed arguments
echo "skip_bin=$skip_bin"
echo "dstdir=$dstdir"
echo "RPM_SLURM_VERSION=$RPM_SLURM_VERSION"

set -euo pipefail

# absolute path to this script (where the spec file is located)
_scriptdir=$(realpath "$(dirname "${BASH_SOURCE[0]}")")

# the project root directory, where `build.sh` checked out the git repo
_projectdir=./src

SLURM_UENV_MOUNT_VERSION=$(sed 's/-.*//' "${_projectdir}/VERSION")

rm -rf "${dstdir}"
mkdir -p "${dstdir}"

echo SLURM_UENV_MOUNT_VERSION=$SLURM_UENV_MOUNT_VERSION
echo _scriptdir=$_scriptdir
echo _projectdir=$_projectdir
tarball=uenv-"${SLURM_UENV_MOUNT_VERSION}".tar.gz

(
  cd "${dstdir}"

  mkdir -p BUILD BUILDROOT RPMS SOURCES SPECS SRPMS

  source_prefix="uenv-${SLURM_UENV_MOUNT_VERSION}"

  (
    cd "${_projectdir}"
    git archive --format=tar.gz --output="${dstdir}/SOURCES/${tarball}" HEAD
  )

  cp "${_scriptdir}/uenv.spec" SPECS/
  sed -i "s|UENVMNT_VERSION|${SLURM_UENV_MOUNT_VERSION}|g" SPECS/uenv.spec
  sed -i "s|RPM_SLURM_VERSION|${RPM_SLURM_VERSION}|g" SPECS/uenv.spec

  # create src rpm
  rpmbuild -bs --define "_topdir ." SPECS/uenv.spec

  if [ "${skip_bin}" -eq "0" ]; then
    # create binary rpm
    rpmbuild -vv --nodeps --define "_topdir $(pwd)" \
             --rebuild SRPMS/uenv-*.src.rpm
  fi
)
