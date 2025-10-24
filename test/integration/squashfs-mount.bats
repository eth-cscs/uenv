function setup() {
    # set the cluster name to be arapiles
    # this is required for tests to work when run on a vCluster
    # that sets this variable
    set -u
    export CLUSTER_NAME=arapiles

    #echo "BATS_LIB_PATH $BATS_LIB_PATH" 1>&3
    bats_load_library bats-support
    bats_load_library bats-assert
    load ./common

    export PATH="$UENV_BIN_PATH:$PATH"

    unset UENV_MOUNT_LIST

    # set up location for creation of working repos
    export TMP=$DATA/scratch
    rm -rf $TMP
    mkdir -p $TMP
}

function teardown() {
    :
}

@test "noargs" {
    run squashfs-mount
    assert_output --partial "no command given"
    assert_failure
}

@test "--version" {
    run uenv --version
    assert_output "$(cat $SRC_PATH/VERSION)"
    assert_success
}

@test "noop" {
    # check that no namespace is unshared, when nothing is mounted
    original_mnt=$(readlink /proc/$$/ns/mnt)
    run bash -c $(which squashfs-mount)' -- readlink /proc/$$/ns/mnt'
    assert_output --partial "${original_mnt}"
}

@test "fwd_env_ld_library_path" {
    # check forwarding for LD_LIBRARY_PATH
    run bash -c 'SQFSMNT_FWD_LD_LIBRARY_PATH=foo squashfs-mount -- sh -c "env | grep ^LD_LIBRARY_PATH="'
    assert_output "LD_LIBRARY_PATH=foo"
}

@test "fwd_non_posix_envvars" {
    # check that exported bash functions (which have non-posix names) are forwarded correctly
    hello() { echo "hello world $1"; }
    export -f hello
    run squashfs-mount -- bash -c 'hello hpc'
    assert_output "hello world hpc"

    # check that non-posix names are forwarded
    run env 'SCRATCH.OLD=/capstor/scratch/robert' squashfs-mount -- printenv 'SCRATCH.OLD'
    assert_output "/capstor/scratch/robert"
}

@test "mount_single_image" {
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    run squashfs-mount --sqfs=$SQFS_PATH/app42.squashfs:/user-environment -- /user-environment/env/app/bin/app
    assert_output "hello app"
    assert_success
}

@test "mount_two_images" {
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    SQFS_MOUNTS=$SQFS_PATH/app42.squashfs:/user-environment,$SQFS_PATH/tool.squashfs:/user-tools
    run squashfs-mount --sqfs=$SQFS_MOUNTS -- /user-environment/env/app/bin/app
    assert_output "hello app"
    assert_success

    run squashfs-mount --sqfs=$SQFS_MOUNTS -- /user-tools/env/tool/bin/tool
    assert_output "hello tool"
    assert_success

    run squashfs-mount --sqfs=$SQFS_MOUNTS -- findmnt --noheadings /user-environment
    assert_line --regexp "/user-environment.*squashfs.*[, ]ro,.*nosuid"
    assert_success

    run squashfs-mount --sqfs=$SQFS_MOUNTS -- findmnt --noheadings /user-tools
    assert_line --regexp "/user-tools.*squashfs.*[, ]ro,.*nosuid"
    assert_success
}

# verify that two images on the same mount point is treated as an error
@test "repeated mount point" {
    SQFS_PATH=$SQFS_LIB/apptool/standalone

    run squashfs-mount --sqfs=$SQFS_PATH/app42.squashfs:/user-environment,$SQFS_PATH/tool.squashfs:/user-environment -- true
    assert_output --partial "the mount point /user-environment is used to mount more than one squashfs"
    assert_failure
}

# verify that mounting an image inside another works
@test "recursive mount" {
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    SQFS=$SQFS_PATH/tool.squashfs
    # pass the inputs in the a different order that they have to be mounted
    run squashfs-mount --sqfs=$SQFS:/user-environment/meta/meta,$SQFS:/user-environment/meta,$SQFS:/user-environment/meta/meta/meta,$SQFS:/user-environment -- realpath /user-environment/meta/meta/meta/meta
    assert_output "/user-environment/meta/meta/meta/meta"
    assert_success
}

