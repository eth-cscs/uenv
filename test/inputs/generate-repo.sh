#!/bin/bash

# clean up previous builds before starting
rm -rf *.squashfs
repo=$(realpath ../scratch/repo)
rm -rf $repo


# create an empty repo
mkdir -p $repo
echo "created repo in $repo"

cp schema.sql.template schema.sql

# create a squashfs image for each uenv
# and copy into the repo
for name in app tool
do
    sqfs=./${name}.squashfs
    mksquashfs ./${name}-uenv ${sqfs} > /dev/null
    sha=$(sha256sum ${sqfs} | awk '{print $1}')
    id=${sha:0:16}

    echo ${sha} ${name}

    img_path=$repo/images/${sha}
    mkdir -p $img_path
    mv ${sqfs} $img_path/store.squashfs
    cp -R ./${name}-uenv/meta $img_path

    sed -i "s|{${name}-sha}|${sha}|g" schema.sql
    sed -i "s|{${name}-id}|${id}|g" schema.sql
done

# create the database
sqlite3 $repo/index.db < schema.sql
