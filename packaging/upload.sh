#!/bin/bash

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

uenv_version=$(cat ${script_dir}/../VERSION)
token=$(cat $HOME/.ssh/jfrog_token)

oslist='15.5 15.6'
slurmlist=$(grep -v '^#' $script_dir/slurm-versions | grep -v '^$' | tr '\n' ' ')

rm -rf ${script_dir}/artifacts/cache

for os in $oslist
do
    for arch in aarch64 x86_64
    do
        cache_path=${script_dir}/artifacts/cache/${os}
        mkdir -p ${cache_path}

        echo "SLES $os $arch"
        for slurm in $slurmlist
        do
            echo "  Slurm version $slurm"
            # convert 23.07.7 to 2307
            uenv_release=slurm$(echo $slurm | sed 's|\.[0-9]\+$||; s|\.||')
            dst_rpm=uenv-${uenv_version}-${uenv_release}.${arch}.rpm
            src_rpm=uenv-${uenv_version}-${uenv_release}.SLES${os}.${arch}.rpm
            dst_url=https://jfrog.svc.cscs.ch/artifactory/cscs-opensuse-${os}-rpm/uenv/${arch}/${dst_rpm}
            src_url=https://github.com/eth-cscs/uenv/releases/download/v${uenv_version}/${src_rpm}
            file=${cache_path}/${dst_rpm}

            echo "    downloading ${os}/${dst_rpm} from ${src_url}"
            curl -sSL -o ${file} ${src_url}
            echo "    uploading   ${os}/${dst_rpm}"
            curl -s -u$USER:$token -XPUT ${dst_url} --upload-file ${file} -o /dev/null
        done
    done
done

