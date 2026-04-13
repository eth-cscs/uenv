#!/usr/bin/env bash

# Build RPMs for all supported OS and Slurm version combinations.
# Slurm versions are read from packaging/slurm-versions.
# Run on each target architecture (x86_64 and aarch64).

set -euo pipefail

scriptdir=$(realpath "$(dirname "${BASH_SOURCE[0]}")")
registry="ghcr.io/eth-cscs"

arch=$(uname -m)
oslist="15.5 15.6"
slurmlist=$(grep -v '^#' "$scriptdir/slurm-versions" | grep -v '^$')

echo "==== creating RPMs for uenv $(cat "$scriptdir/../VERSION")"
echo "     arch:  $arch"
echo "     os:    $oslist"
echo "     slurm: $(echo $slurmlist | tr '\n' ' ')"

for os in $oslist; do
    container="$registry/uenv-build-sles$os:$arch"
    echo "--- pulling $container"
    podman pull "$container"
    for slurm in $slurmlist; do
        echo "--- building RPM: SLES $os, Slurm $slurm"
        podman run --rm \
            -v "$scriptdir:/work:rw" \
            -w /work \
            -v "$(realpath "$scriptdir/.."):/source:ro" \
            "$container" \
            sh -c "./suse-build.sh --slurm-version=$slurm --ref=HEAD --os=$os" \
            >& "$scriptdir/log-$os-$slurm-$arch"
    done
done
