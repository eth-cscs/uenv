#!/bin/bash

set -euo pipefail

usage="$(basename "$0") [-h]  [--slurm-version] [--remote] -r,--ref dstdir

Helper script downloads required slurm headers and calls rpmbuild-wrapper.sh.

Options:
-h,--help          show this help text
-r,--ref           git reference
--remote           uenv2 github repo (defaults to eth-cscs/uenv2)
--slurm-version    slurm version to specify in RPM dependency
"

# A temporary variable to hold the output of `getopt`
TEMP=$(getopt -o r:h --long ref:,remote:,help,slurm-version: -- "$@")

# default Slurm version is 0 -> use system slurm
slurm_version="00.00.0"
remote=https://github.com/eth-cscs/uenv2

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
    -r | --ref)
        shift
        git_ref="$1"
        shift
        ;;
    --remote)
        shift
        remote="$1"
        shift
        ;;
    --slurm-version)
        shift
        slurm_version="$1"
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

_scriptdir=$(realpath "$(dirname "${BASH_SOURCE[0]}")")

wd=$(mktemp -d)

if [ -z ${git_ref+x} ]; then
    >&2 echo "-r,--ref must be passed."
    exit 1
fi

(
    cd $wd || exit 1
    git clone -b $git_ref $remote src

    FLAGS="-O2 -fmessage-length=0 -D_FORTIFY_SOURCE=2 -fstack-protector"
    # Build RPM
    mkdir rpmbuild
    LDFLAGS="-Wl,--disable-new-dtags -Wl,-rpath,/lib64 -Wl,-rpath,/usr/lib64" \
        CXXFLAGS="$FLAGS" \
        CFLAGS="$FLAGS" \
        ${_scriptdir}/rpmbuild-wrapper.sh --slurm-version=$slurm_version ./src ./rpmbuild \
        2>${_scriptdir}/stderr.log 1>${_scriptdir}/stdout.log

  set -x
    find ./rpmbuild/RPMS -iname '*.rpm' -exec cp {} ${_scriptdir} \;
  set +x
)
