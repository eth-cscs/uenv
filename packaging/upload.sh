#!/bin/bash

uenv_version=$(cat ../VERSION)
token=$(cat ~/.ssh/jfrog_token)
arch=$(uname -m)

oslist='15.5 15.6'
slurmlist='23.02.7 24.05.4 25.05.4'

for os in $oslist
do
    echo "SLES $os"
    for slurm in $slurmlist
    do
        # convert 23.07.7 to 2307
        uenv_release=slurm$(echo $slurm | sed 's|\.[0-9]\+$||; s|\.||')
        rpm=uenv-${uenv_version}-${uenv_release}.${arch}.rpm

        url=https://jfrog.svc.cscs.ch/artifactory/cscs-opensuse-${os}-rpm/uenv/${arch}/${rpm}
        file=artifacts/opensuse-${os}/${arch}/${rpm}

        curl -u$USER:$token -XPUT $url --upload-file $file
    done
done

