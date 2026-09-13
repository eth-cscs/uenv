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
    reset_scratch_dir $TMP
}

function teardown() {
    # the root-owned fixtures live in a root-owned mode-700 directory, so
    # setup()'s rm -rf cannot clear them if a test failed before its own
    # cleanup ran.
    if [ -d "${TMP:-}/rootonly" ] && sudo -n true 2>/dev/null; then
        sudo -n rm -rf "$TMP/rootonly"
    fi
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

@test "squashfs-mount drops back to the calling user (not root), nothing mounted" {
    # only meaningful for the setuid (kernel squashfs) build: the fuse build
    # is never installed setuid and refuses to run if it is.
    squashfs_mount_bin=$(command -v squashfs-mount)
    if [[ ! -u "$squashfs_mount_bin" ]]; then
        skip "squashfs-mount is not installed setuid"
    fi

    my_uid=$(id -u)
    run bash -c "$squashfs_mount_bin"' -- sh -c "id -u; id -ru"'
    assert_success
    assert_output "$my_uid
$my_uid"
}

@test "squashfs-mount sets NoNewPrivs before exec, nothing mounted" {
    squashfs_mount_bin=$(command -v squashfs-mount)
    if [[ ! -u "$squashfs_mount_bin" ]]; then
        skip "squashfs-mount is not installed setuid"
    fi

    run bash -c "$squashfs_mount_bin"' -- grep NoNewPrivs /proc/self/status'
    assert_success
    assert_line --regexp '^NoNewPrivs:[[:space:]]+1$'
}

@test "squashfs-mount drops back to the calling user (not root), after mounting" {
    squashfs_mount_bin=$(command -v squashfs-mount)
    if [[ ! -u "$squashfs_mount_bin" ]]; then
        skip "squashfs-mount is not installed setuid"
    fi

    my_uid=$(id -u)
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    run squashfs-mount --sqfs=$SQFS_PATH/app42.squashfs:/user-environment -- sh -c "id -u; id -ru"
    assert_success
    assert_output "$my_uid
$my_uid"
}

@test "squashfs-mount sets NoNewPrivs before exec, after mounting" {
    squashfs_mount_bin=$(command -v squashfs-mount)
    if [[ ! -u "$squashfs_mount_bin" ]]; then
        skip "squashfs-mount is not installed setuid"
    fi

    SQFS_PATH=$SQFS_LIB/apptool/standalone
    run squashfs-mount --sqfs=$SQFS_PATH/app42.squashfs:/user-environment -- grep NoNewPrivs /proc/self/status
    assert_success
    assert_line --regexp '^NoNewPrivs:[[:space:]]+1$'
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
    assert_line --regexp "(/user-environment.*squashfs.*[, ]ro,.*nosuid|/user-environment[[:space:]]+/dev/fuse[[:space:]]+fuse[[:space:]]+rw,nosuid.*)"
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


#
# the calling user's access to the image is what governs the mount
#
# squashfs-mount is setuid root, so it could open any image on the node. These
# tests check that it does not: the images are opened with the calling user's
# credentials, before any privilege is reclaimed.
#

@test "unreadable image is refused" {
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    cp $SQFS_PATH/app42.squashfs $TMP/unreadable.squashfs
    chmod 000 $TMP/unreadable.squashfs

    run squashfs-mount --sqfs=$TMP/unreadable.squashfs:/user-environment -- true
    assert_failure
    assert_output --partial "Permission denied"

    # it must fail before mounting, not after
    run findmnt -r /user-environment
    assert_failure
}

@test "image in an untraversable directory is refused" {
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    mkdir -p $TMP/closed
    cp $SQFS_PATH/app42.squashfs $TMP/closed/image.squashfs
    # the image itself is world readable: only the directory is closed, which
    # is the half of the bypass a file-mode check would miss
    chmod 644 $TMP/closed/image.squashfs
    chmod 000 $TMP/closed

    run squashfs-mount --sqfs=$TMP/closed/image.squashfs:/user-environment -- true
    chmod 700 $TMP/closed

    assert_failure
    assert_output --partial "Permission denied"
}

@test "root-owned image is refused" {
    squashfs_mount_bin=$(command -v squashfs-mount)
    if [[ ! -u "$squashfs_mount_bin" ]]; then
        skip "squashfs-mount is not installed setuid"
    fi

    SQFS_PATH=$SQFS_LIB/apptool/standalone
    make_root_only_image $SQFS_PATH/app42.squashfs $TMP/rootonly

    run squashfs-mount --sqfs=$ROOT_ONLY_IMAGE:/user-environment -- true
    remove_root_only_image $TMP/rootonly

    assert_failure
    refute_output --partial "hello app"
}

@test "a root-owned image is not an existence oracle" {
    squashfs_mount_bin=$(command -v squashfs-mount)
    if [[ ! -u "$squashfs_mount_bin" ]]; then
        skip "squashfs-mount is not installed setuid"
    fi

    SQFS_PATH=$SQFS_LIB/apptool/standalone
    make_root_only_image $SQFS_PATH/app42.squashfs $TMP/rootonly

    # A file that exists and one that does not, both inside a directory the
    # caller cannot traverse, must fail the same way. As root the helper could
    # tell them apart and hand the difference back to the caller, which makes
    # it an existence oracle for any path on the node.
    run squashfs-mount --sqfs=$ROOT_ONLY_IMAGE:/user-environment -- true
    assert_failure
    assert_output --partial "Permission denied"
    refute_output --partial "No such file"

    run squashfs-mount --sqfs=$TMP/rootonly/absent.squashfs:/user-environment -- true
    remove_root_only_image $TMP/rootonly
    assert_failure
    assert_output --partial "Permission denied"
    refute_output --partial "No such file"
}

@test "squashfs-mount keeps the calling user's gid, after mounting" {
    squashfs_mount_bin=$(command -v squashfs-mount)
    if [[ ! -u "$squashfs_mount_bin" ]]; then
        skip "squashfs-mount is not installed setuid"
    fi

    # The binary is setuid but not setgid, so the caller's group set is
    # already correct and must survive to the exec'd command. A setegid()
    # introduced around the mount, or a gid dropped in the wrong order, would
    # show up here.
    my_gid=$(id -g)
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    run squashfs-mount --sqfs=$SQFS_PATH/app42.squashfs:/user-environment -- sh -c "id -g; id -rg"
    assert_success
    assert_output "$my_gid
$my_gid"
}

@test "group-readable image mounts" {
    # Positive control for the group half of the access check: an image the
    # caller reaches only through a supplementary group must still mount. This
    # is what a check that used the primary gid alone would wrongly refuse.
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    make_group_readable_image $SQFS_PATH/app42.squashfs $TMP/group.squashfs

    run squashfs-mount --sqfs=$GROUP_IMAGE:/user-environment -- /user-environment/env/app/bin/app
    assert_output "hello app"
    assert_success
}
