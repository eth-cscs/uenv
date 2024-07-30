#!/bin/bash

# clean up previous builds before starting
rm -rf *.squashfs
repo=$(realpath ../scratch/repo)
rm -rf $repo


# create an empty repo
mkdir -p $repo
echo "created repo in $repo"

# TODO: create an empty index.db database

# create a squashfs image for each uenv
# and copy into the repo
for name in app tool
do
    sqfs=./${name}.squashfs
    mksquashfs ./${name}-uenv ${sqfs} > /dev/null
    sha=$(sha256sum ${sqfs} | awk '{print $1}')

    echo ${sha} ${name}

    mkdir -p $repo/${sha}
    mv ${sqfs} $repo/${sha}/store.squashfs
    cp -R ./${name}-uenv/meta $repo/${sha}

    # TODO: create an entry for this image in the database
done
