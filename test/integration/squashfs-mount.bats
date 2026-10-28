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

#
# -r/--mutable-root and --bind-mount
#
# Both flags exist only in the fuse/rootless backend: the kernel backend's
# parser does not define them at all, and passing them there is a plain
# parse error rather than a meaningful skip. --help output is a reliable way
# to tell the backends apart, whether or not the binary happens to be
# installed setuid (that bit only distinguishes kernel builds that *are*
# installed from ones that are not).
#
# Mount points/destinations below deliberately use fresh top-level names
# (never nested under an existing top-level directory such as $TMP, which
# lives under /home) - make_mutable_root() excludes a dst's top-level
# component from the rebuilt "/" whenever that component already exists on
# the host but the dst itself does not, which would also hide anything else
# under that same top-level component (e.g. a --bind-mount source living
# under $TMP). A brand new top-level name avoids that exclusion entirely, and
# nothing created under it ever touches the real host filesystem: the whole
# rebuild happens inside the process's own unshared mount namespace.
#

function require_fuse_backend() {
    run squashfs-mount --help
    if [[ "$output" != *"mutable-root"* ]]; then
        skip "squashfs-mount is not built with the fuse backend (no mutable-root/bind-mount support)"
    fi
}

@test "mutable root: --sqfs mounts on a mount point that does not exist" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone

    run squashfs-mount -r --sqfs=$SQFS_PATH/app42.squashfs:/uenv-bats-mutable-root-mount -- /uenv-bats-mutable-root-mount/env/app/bin/app
    assert_output "hello app"
    assert_success
}

@test "without -r, a --sqfs mount point that does not exist is refused" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone

    run squashfs-mount --sqfs=$SQFS_PATH/app42.squashfs:/uenv-bats-no-mutable-root-mount -- true
    assert_failure
    assert_output --partial "does not exist"
}

@test "--bind-mount to a destination that already exists" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc $TMP/binddst
    echo "bind-mount-content" > $TMP/bindsrc/file.txt

    run squashfs-mount --bind-mount=$TMP/bindsrc:$TMP/binddst -- cat $TMP/binddst/file.txt
    assert_output "bind-mount-content"
    assert_success
}

@test "without -r, a --bind-mount destination that does not exist is refused" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc

    run squashfs-mount --bind-mount=$TMP/bindsrc:/uenv-bats-no-mutable-root-bind -- true
    assert_failure
    assert_output --partial "does not exist"
}

@test "mutable root: --bind-mount to a destination that does not exist" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc
    echo "bind-mount-content" > $TMP/bindsrc/file.txt

    run squashfs-mount -r --bind-mount=$TMP/bindsrc:/uenv-bats-mutable-root-bind -- cat /uenv-bats-mutable-root-bind/file.txt
    assert_output "bind-mount-content"
    assert_success
}

@test "--bind-mount cannot be repeated: use a comma separated list" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc1 $TMP/bindsrc2 $TMP/binddst1 $TMP/binddst2

    run squashfs-mount \
        --bind-mount=$TMP/bindsrc1:$TMP/binddst1 \
        --bind-mount=$TMP/bindsrc2:$TMP/binddst2 \
        -- true
    assert_failure
    assert_output --partial "can only be given once"
}

@test "--bind-mount accepts a comma separated list, like --sqfs" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc1 $TMP/bindsrc2 $TMP/binddst1 $TMP/binddst2
    echo "first" > $TMP/bindsrc1/file.txt
    echo "second" > $TMP/bindsrc2/file.txt

    run squashfs-mount \
        --bind-mount=$TMP/bindsrc1:$TMP/binddst1,$TMP/bindsrc2:$TMP/binddst2 \
        -- sh -c "cat $TMP/binddst1/file.txt && cat $TMP/binddst2/file.txt"
    assert_output "first
second"
    assert_success
}

@test "mutable root: --sqfs, --bind-mount and a command with its own flags together" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    mkdir -p $TMP/bindsrc
    echo "bind-mount-content" > $TMP/bindsrc/file.txt

    # regression test: --bind-mount used to swallow the "--" that separates
    # squashfs-mount's own options from the exec'd command, so a command
    # using its own short option (here `sh -c`) was misparsed as an
    # unrecognized option of squashfs-mount itself.
    run squashfs-mount -r \
        --sqfs=$SQFS_PATH/app42.squashfs:/uenv-bats-combo-mount \
        --bind-mount=$TMP/bindsrc:/uenv-bats-combo-bind \
        -- sh -c "cat /uenv-bats-combo-bind/file.txt && /uenv-bats-combo-mount/env/app/bin/app"
    assert_output "bind-mount-content
hello app"
    assert_success
}

#
# --sqfs/--bind-mount/--tmpfs are applied in the order produced by a lexical,
# stable sort of their destination paths, like bubblewrap's nesting
# semantics but without needing to track what order the flags were actually
# typed in: a parent destination's path always sorts before anything nested
# under it, which is what lets a --tmpfs mount correctly bury a --bind-mount
# nested under it, a --tmpfs land inside a squashfs image, or vice versa,
# regardless of flag order. Two entries with the identical destination are
# an error rather than a pick between them.
#

@test "--bind-mount and --tmpfs at an identical destination is refused" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc

    run squashfs-mount -r \
        --bind-mount=$TMP/bindsrc:/uenv-bats-order-mount \
        --tmpfs=/uenv-bats-order-mount \
        -- true
    assert_failure
    assert_output --partial "more than one --sqfs/--bind-mount/--tmpfs"

    # same error regardless of flag order
    run squashfs-mount -r \
        --tmpfs=/uenv-bats-order-mount \
        --bind-mount=$TMP/bindsrc:/uenv-bats-order-mount \
        -- true
    assert_failure
    assert_output --partial "more than one --sqfs/--bind-mount/--tmpfs"
}

@test "a --bind-mount nested under a --tmpfs is visible on top of it, regardless of flag order" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc
    echo "bind-content" > $TMP/bindsrc/file.txt

    run squashfs-mount -r \
        --bind-mount=$TMP/bindsrc:/uenv-bats-order-mount/child \
        --tmpfs=/uenv-bats-order-mount \
        -- cat /uenv-bats-order-mount/child/file.txt
    assert_output "bind-content"
    assert_success

    run squashfs-mount -r \
        --tmpfs=/uenv-bats-order-mount \
        --bind-mount=$TMP/bindsrc:/uenv-bats-order-mount/child \
        -- cat /uenv-bats-order-mount/child/file.txt
    assert_output "bind-content"
    assert_success
}

@test "a --tmpfs nested under an --sqfs mount point is mounted inside the image, regardless of flag order" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone

    # /env exists inside the image: the tmpfs hides its content and is writable
    run squashfs-mount -r \
        --tmpfs=/uenv-bats-sqfs-tmpfs/env \
        --sqfs=$SQFS_PATH/app42.squashfs:/uenv-bats-sqfs-tmpfs \
        -- sh -c "touch /uenv-bats-sqfs-tmpfs/env/new && ls /uenv-bats-sqfs-tmpfs/env && ls /uenv-bats-sqfs-tmpfs/meta"
    assert_success
    assert_line --index 0 "new"
    assert_line --index 1 "env.json"
}

@test "a --bind-mount nested under an --sqfs mount point is visible inside the image" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    mkdir -p $TMP/bindsrc
    echo "bind-content" > $TMP/bindsrc/file.txt

    run squashfs-mount -r \
        --bind-mount=$TMP/bindsrc:/uenv-bats-sqfs-bind/env/app \
        --sqfs=$SQFS_PATH/app42.squashfs:/uenv-bats-sqfs-bind \
        -- cat /uenv-bats-sqfs-bind/env/app/file.txt
    assert_output "bind-content"
    assert_success
}

@test "without -r, a --tmpfs inside an --sqfs mount point is not checked on the host" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    mkdir -p $TMP/sqfs-mnt

    # $TMP/sqfs-mnt/env does not exist on the host, only inside the image
    run squashfs-mount \
        --sqfs=$SQFS_PATH/app42.squashfs:$TMP/sqfs-mnt \
        --tmpfs=$TMP/sqfs-mnt/env \
        -- sh -c "touch $TMP/sqfs-mnt/env/new && ls $TMP/sqfs-mnt/env"
    assert_output "new"
    assert_success
}

@test "a mount point missing inside an --sqfs image is an error" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone

    # the image is read only, so a mutable root can not create it either
    run squashfs-mount -r \
        --sqfs=$SQFS_PATH/app42.squashfs:/uenv-bats-sqfs-missing \
        --tmpfs=/uenv-bats-sqfs-missing/not-in-image \
        -- true
    assert_failure
    assert_output --partial "/uenv-bats-sqfs-missing/not-in-image"
}

@test "--tmpfs above an --sqfs mount point is fine" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone

    # the tmpfs is mounted first, then the image inside it
    run squashfs-mount -r \
        --tmpfs=/uenv-bats-shadow-sqfs-ok \
        --sqfs=$SQFS_PATH/app42.squashfs:/uenv-bats-shadow-sqfs-ok/inner \
        -- /uenv-bats-shadow-sqfs-ok/inner/env/app/bin/app
    assert_output "hello app"
    assert_success
}

# A --bind-mount reads its source when it is mounted, so a source inside
# another destination must be mounted after that destination, even when its
# own destination sorts first.

@test "a --bind-mount whose source is inside an --sqfs mount point binds the image's directory" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    # $TMP/a-alias sorts before $TMP/sqfs-src; $TMP/sqfs-src/env only exists
    # inside the image
    mkdir -p $TMP/a-alias $TMP/sqfs-src

    run squashfs-mount \
        --sqfs=$SQFS_PATH/app42.squashfs:$TMP/sqfs-src \
        --bind-mount=$TMP/sqfs-src/env:$TMP/a-alias \
        -- $TMP/a-alias/app/bin/app
    assert_output "hello app"
    assert_success
}

@test "a --bind-mount whose source is the directory of another --bind-mount sees its content" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc/sub $TMP/a-alias $TMP/data
    echo "nested-content" > $TMP/bindsrc/sub/file.txt

    # $TMP/a-alias sorts before $TMP/data, its source $TMP/data/sub only
    # exists once $TMP/bindsrc is bound at $TMP/data
    run squashfs-mount \
        --bind-mount=$TMP/data/sub:$TMP/a-alias,$TMP/bindsrc:$TMP/data \
        -- cat $TMP/a-alias/file.txt
    assert_output "nested-content"
    assert_success
}

@test "--bind-mounts whose sources are inside each other's destination are refused" {
    require_fuse_backend
    mkdir -p $TMP/a/y $TMP/b/x

    run squashfs-mount \
        --bind-mount=$TMP/b/x:$TMP/a,$TMP/a/y:$TMP/b \
        -- true
    assert_failure
}

# Destinations are compared as written: a trailing separator, "." or ".."
# would otherwise defeat the duplicate check and the parent-before-child
# order.

@test "a trailing separator does not hide a duplicate destination" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc $TMP/dup

    run squashfs-mount \
        --tmpfs=$TMP/dup/ \
        --bind-mount=$TMP/bindsrc:$TMP/dup \
        -- true
    assert_failure
    assert_output --partial "more than one --sqfs/--bind-mount/--tmpfs"
}

@test "a '.' or '..' component does not hide a duplicate destination" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc $TMP/dup $TMP/other

    run squashfs-mount \
        --tmpfs=$TMP/./dup \
        --bind-mount=$TMP/bindsrc:$TMP/other/../dup \
        -- true
    assert_failure
    assert_output --partial "more than one --sqfs/--bind-mount/--tmpfs"
}

@test "a '..' component does not break the parent-before-child order" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    mkdir -p $TMP/sq $TMP/z

    # $TMP/z/../sq is the parent of $TMP/sq/env, but sorts after it as
    # written: mounted second, the image would hide the tmpfs. env exists
    # inside the image, so this does not depend on creating mount points.
    run squashfs-mount \
        --sqfs=$SQFS_PATH/app42.squashfs:$TMP/z/../sq \
        --tmpfs=$TMP/sq/env \
        -- sh -c "touch $TMP/sq/env/new && ls $TMP/sq/env && ls $TMP/sq/meta"
    assert_success
    assert_line --index 0 "new"
    assert_line --index 1 "env.json"
}

@test "a relative --tmpfs or --bind-mount path is resolved against the working directory" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc $TMP/cwd/rel
    echo "bind-content" > $TMP/bindsrc/file.txt
    cd $TMP/cwd

    run squashfs-mount --tmpfs=./rel -- sh -c "touch $TMP/cwd/rel/new && ls $TMP/cwd/rel"
    assert_output "new"
    assert_success

    run squashfs-mount --bind-mount=$TMP/bindsrc:./rel -- cat $TMP/cwd/rel/file.txt
    assert_output "bind-content"
    assert_success

    # the relative and the absolute spelling of a directory are the same
    # destination
    run squashfs-mount \
        --tmpfs=./rel \
        --bind-mount=$TMP/bindsrc:$TMP/cwd/rel \
        -- true
    assert_failure
    assert_output --partial "more than one --sqfs/--bind-mount/--tmpfs"
}

@test "a symlinked spelling does not hide a duplicate destination" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    mkdir -p $TMP/real/mnt
    ln -s real $TMP/link

    run squashfs-mount \
        --sqfs=$SQFS_PATH/app42.squashfs:$TMP/link/mnt \
        --tmpfs=$TMP/real/mnt \
        -- true
    assert_failure
    assert_output --partial "more than one --sqfs/--bind-mount/--tmpfs"
}

# A --tmpfs is empty and always writable: a mount point nested in it does not
# exist until it is created there, with or without -r.

@test "without -r, a --bind-mount nested in a --tmpfs is mounted, whether or not its destination exists on the host" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc $TMP/tmpfs-mnt/exists
    echo "bind-content" > $TMP/bindsrc/file.txt

    # exists on the host, but is hidden by the tmpfs
    run squashfs-mount \
        --tmpfs=$TMP/tmpfs-mnt \
        --bind-mount=$TMP/bindsrc:$TMP/tmpfs-mnt/exists \
        -- cat $TMP/tmpfs-mnt/exists/file.txt
    assert_output "bind-content"
    assert_success

    # does not exist on the host
    run squashfs-mount \
        --tmpfs=$TMP/tmpfs-mnt \
        --bind-mount=$TMP/bindsrc:$TMP/tmpfs-mnt/missing/deeper \
        -- cat $TMP/tmpfs-mnt/missing/deeper/file.txt
    assert_output "bind-content"
    assert_success
    # nothing was created on the host
    [ ! -e $TMP/tmpfs-mnt/missing ]
}

@test "without -r, an --sqfs nested in a --tmpfs is mounted, whether or not its mount point exists on the host" {
    require_fuse_backend
    SQFS_PATH=$SQFS_LIB/apptool/standalone
    mkdir -p $TMP/tmpfs-mnt/exists

    run squashfs-mount \
        --tmpfs=$TMP/tmpfs-mnt \
        --sqfs=$SQFS_PATH/app42.squashfs:$TMP/tmpfs-mnt/exists \
        -- $TMP/tmpfs-mnt/exists/env/app/bin/app
    assert_output "hello app"
    assert_success

    run squashfs-mount \
        --tmpfs=$TMP/tmpfs-mnt \
        --sqfs=$SQFS_PATH/app42.squashfs:$TMP/tmpfs-mnt/missing \
        -- $TMP/tmpfs-mnt/missing/env/app/bin/app
    assert_output "hello app"
    assert_success
    [ ! -e $TMP/tmpfs-mnt/missing ]
}

@test "without -r, a --tmpfs nested in a --tmpfs is mounted, whether or not its mount point exists on the host" {
    require_fuse_backend
    mkdir -p $TMP/tmpfs-mnt

    run squashfs-mount \
        --tmpfs=$TMP/tmpfs-mnt,$TMP/tmpfs-mnt/inner \
        -- sh -c "touch $TMP/tmpfs-mnt/inner/new && ls $TMP/tmpfs-mnt/inner"
    assert_output "new"
    assert_success
    [ ! -e $TMP/tmpfs-mnt/inner ]
}

@test "with -r, a mount point missing inside a --bind-mount is not created in its source" {
    require_fuse_backend
    mkdir -p $TMP/bindsrc

    # creating the mount point would write into $TMP/bindsrc on the host
    run squashfs-mount -r \
        --bind-mount=$TMP/bindsrc:/uenv-bats-bind-provider \
        --tmpfs=/uenv-bats-bind-provider/new \
        -- true
    assert_failure
    [ ! -e $TMP/bindsrc/new ]
}
