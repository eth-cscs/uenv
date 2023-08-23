#!/bin/bash

function setup() {
    export test_path="$( cd "$( dirname "$BATS_TEST_FILENAME" )" >/dev/null 2>&1 && pwd )"
    export repo_path=$(realpath "$test_path/..")

    bats_load_library bats-support
    bats_load_library bats-assert

    export base_path="$test_path/tmp"
    export image_path="$base_path/images"
    export mount_path="$base_path/mnt"

    # make a backup of ~/.bashrc
    echo "=== make a back up of ~/.bashrc -> $base_path/bashrc"
    cp ~/.bashrc "$base_path/bashrc"

    "$repo_path/install" --prefix="$repo_path" --yes

    [[ -d "$base_path/bin" ]] && export PATH="$base_path/bin:$PATH"
    echo "BIN: $(ls $base_path/bin)"

    echo "SQUASHFS-MOUNT BINARY: $(which squashfs-mount)"
}

function teardown() {
    echo "=== reset bashrc"

    cp "$base_path/bashrc" ~/.bashrc
}

@test "mount_single_image" {
    squashfs-mount ${image_path}/uenv.sqashfs:/user-environment -- cat /user-environment/file.txt
}
