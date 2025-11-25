#!/usr/bin/env bash

set -euo pipefail

#rm -rf ./artifacts

oslist='15.5 15.6'
slurmlist='23.02.7 24.05.4 24.05.8 25.05.4'

echo "==== creating RPMS for uenv $(cat ../VERSION)"
arch=$(uname -m)
for os in $oslist
do
    echo "SLES $os"
    container=ghcr.io/bcumming/uenv-build-sles$os:$arch
    echo "pulling $container..."
    podman pull $container
    #container=uenv-sles$os
    #podman build -f ./dockerfiles/sles$os . -t $container >& log-$os
    for slurm in $slurmlist
    do
        echo "    Slurm version $slurm"
        podman run -v $(pwd):/work:rw -w /work -v $(realpath ..):/source:rw \
            -it $container sh -c "./suse-build.sh --slurm-version=$slurm --ref=HEAD --os=$os" \
                >& log-$os-$slurm-$arch
    done
done

