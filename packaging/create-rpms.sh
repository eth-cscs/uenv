#!/usr/bin/env bash

set -euo pipefail

arch=$(uname -m)
for os in 15.5 15.6
do
    echo "SLES $os"
    container=uenv-sles$os
    podman build -f ./dockerfiles/sles$os . -t $container >& log-$os
    for slurm in 25.05.0 24.05.4 23.02.7
    do
        echo "    Slurm version $slurm"
        podman run -v $(pwd):/work:rw -w /work -v $(realpath ..):/source:rw \
            -it $container:latest                                           \
            sh -c "./suse-build.sh --slurm-version=$slurm --ref=HEAD --os=$os" \
                >& log-$os-$slurm
        #curl -u$USER:$TOKEN -XPUT \
        #    https://jfrog.svc.cscs.ch/artifactory/cscs-opensuse-${os}-rpm/uenv/${arch}/uenv-9.0.0-${slurm}.rpm \
        #    --upload-file rpms/${arch}/uenv-9.0.0-${slurm}.rpm
    done
    #push image
done
