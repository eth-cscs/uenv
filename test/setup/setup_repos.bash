#!/bin/bash

function setup_repo_apptool() {
    scratch=$1
    working=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)

    repo=${scratch}/repos/apptool
    sources=${working}/apptool

    echo "repo path $(repo)"
    echo "working path $working"
    echo "source path $sources"

    # clean up previous builds before starting
    rm -rf ${repo}

    # create an empty repo
    mkdir -p ${repo}

    cp ${sources}/schema.sql.template schema.sql

    # create a squashfs image for each uenv
    # and copy into the repo
    for name in app tool
    do
        sqfs=${working}/${name}.squashfs
        mksquashfs ${sources}/${name}-uenv ${sqfs} > /dev/null
        sha=$(sha256sum ${sqfs} | awk '{print $1}')
        id=${sha:0:16}

        echo ${sha} ${name}

        img_path=$repo/images/${sha}
        mkdir -p $img_path
        mv ${sqfs} $img_path/store.squashfs
        cp -R ${sources}/${name}-uenv/meta $img_path

        sed -i "s|{${name}-sha}|${sha}|g" schema.sql
        sed -i "s|{${name}-id}|${id}|g" schema.sql
    done

    # create the database
    sqlite3 ${repo}/index.db < schema.sql

    rm -rf store.squashfs
    rm schema.sql
}

function setup_repos() {
    echo "= Setup Repos"
    scratch=$1
    echo "scratch path $scratch"

    rm -rf $scratch
    mkdir -p $scratch
    scratch=$(realpath $scratch)

    echo
    echo "== apptool repo"
    setup_repo_apptool $scratch
}
