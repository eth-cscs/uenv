#!/bin/bash

function setup_repo_apptool() {
    scratch=$1
    working=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)

    repo=${scratch}/repos/apptool
    sqfs_path=${scratch}/sqfs/apptool
    sources=${working}/apptool

    # clean up previous builds before starting
    rm -rf ${repo}
    rm -rf ${sqfs_path}

    # create an empty repo
    mkdir -p ${repo}
    mkdir -p ${sqfs_path}

    cp ${sources}/schema.sql.template schema.sql

    # create a squashfs image for each uenv
    # and copy into the repo
    for name in app42 app43 tool
    do
        sqfs=${working}/${name}.squashfs
        mksquashfs ${sources}/${name} ${sqfs} > /dev/null
        sha=$(sha256sum ${sqfs} | awk '{print $1}')
        id=${sha:0:16}

        for img_path in "$repo/images/${sha}" "$sqfs_path/$name"
        do
            mkdir -p $img_path
            cp ${sqfs} $img_path/store.squashfs
            cp -R ${sources}/${name}/meta $img_path
        done

        img_path="$sqfs_path/standalone"
        mkdir -p $img_path
        cp ${sqfs} $img_path/${name}.squashfs

        rm ${sqfs}

        sed -i "s|{${name}-sha}|${sha}|g" schema.sql
        sed -i "s|{${name}-id}|${id}|g" schema.sql
    done

    # create the database
    sqlite3 ${repo}/index.db < schema.sql

    rm -rf store.squashfs
    rm schema.sql
}

function setup_repos() {
    scratch=$1

    rm -rf $scratch
    mkdir -p $scratch
    scratch=$(realpath $scratch)

    setup_repo_apptool $scratch
}
