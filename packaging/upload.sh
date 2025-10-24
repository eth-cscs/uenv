#!/bin/bash

version=$(cat ../VERSION)
token=$(cat ~/.ssh/jfrog_token)
arch=$(uname -m)
for os in 15.5 15.6
do
    echo "SLES $os"
    for slurm in 23.02.7 24.05.4 24.05.8 25.05.0
    do
        rpm=uenv-${version}-${slurm}.${arch}.rpm
        echo "    Slurm version $slurm: $rpm"

        curl -u$USER:$token -XPUT \
             https://jfrog.svc.cscs.ch/artifactory/cscs-opensuse-${os}-rpm/uenv/${arch}/${rpm} \
             --upload-file artifacts/opensuse-${os}/${arch}/${rpm}
    done
done

