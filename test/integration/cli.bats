function setup() {
    bats_install_path=$(realpath ./install)
    export BATS_LIB_PATH=$bats_install_path/bats-helpers

    bats_load_library bats-support
    bats_load_library bats-assert
    load ./common

    export REPOS=$(realpath ../scratch/repos)
    export SQFS_LIB=$(realpath ../scratch/sqfs)

    export SRC_PATH=$(realpath ../../)

    export PATH="$(realpath ../../build):$PATH"

    unset UENV_MOUNT_LIST

    # set up location for creation of working repos
    export REPO_ROOT=/tmp/uenv-repo
    rm -rf $REPO_ROOT
    mkdir -p $REPO_ROOT

    # make a scratch space in the same file system as the repo root
    mkdir -p $REPO_ROOT/scratch

    # remove the bash function uenv, if an older version of uenv is installed on
    # the system
    unset -f uenv
}

function teardown() {
    export REPO_ROOT=/tmp/uenv-repo
    #rm -rf $REPO_ROOT
}

@test "noargs" {
    run uenv
    assert_output --partial "uenv version $(cat $SRC_PATH/VERSION)"
    assert_success
}

@test "--version" {
    run uenv --version
    assert_output "$(cat $SRC_PATH/VERSION)"
    assert_success
}

@test "image ls" {
    export UENV_REPO_PATH=$REPOS/apptool
    export CLUSTER_NAME=arapiles

    run uenv image ls
    assert_success
    assert_line --index 0 --regexp "^uenv\s+arch\s+system\s+id"
    assert_output --regexp "app/42.0:v1\s+zen3\s+arapiles"
    assert_output --regexp "app/43.0:v1\s+zen3\s+arapiles"
    assert_output --regexp "tool/17.3.2:v1\s+zen3\s+arapiles"

    run uenv image ls --no-header
    assert_success
    refute_line --regexp "^uenv\s+arch\s+system\s+id"
    assert_line --regexp "app/42.0:v1\s+zen3\s+arapiles"
    assert_line --regexp "app/43.0:v1\s+zen3\s+arapiles"
    assert_line --regexp "tool/17.3.2:v1\s+zen3\s+arapiles"

    run uenv image ls --no-header app
    assert_success
    assert_line --partial "app/42.0:v1"
    assert_line --partial "app/43.0:v1"
    refute_line --partial "tool/17.3.2:v1"

    run uenv image ls --no-header app/43.0
    assert_success
    refute_line --partial "app/42.0:v1"
    assert_line --partial "app/43.0:v1"
    refute_line --partial "tool/17.3.2:v1"

    run uenv image ls --no-header tool
    assert_success
    refute_line --partial "app/42.0:v1"
    refute_line --partial "app/43.0:v1"
    assert_line --partial "tool/17.3.2:v1"

    run uenv image ls wombat
    assert_success
    assert_output "no matching uenv"

    # empty output if --no-header is used and there are no matches
    run uenv image ls wombat --no-header
    assert_success
    assert_output ""

    # unset the UENV_REPO_PATH variable and use the --repo flag instead
    unset UENV_REPO_PATH

    run uenv --repo=/wombat image ls --no-header
    assert_failure
    assert_output --partial "the repository /wombat does not exist"

    run uenv --repo=$REPOS/apptool image ls --no-header
    assert_success
    assert_line --partial "app/42.0:v1"
    assert_line --partial "app/43.0:v1"
    assert_line --partial "tool/17.3.2:v1"
}

@test "repo status" {
    export RP=$REPOS/apptool

    #
    # check the different methods for providing the repo location
    #

    # using UENV_REPO_PATH env variable
    UENV_REPO_PATH=$RP run uenv repo status
    assert_success
    assert_line --index 0 "the repository at $RP is read-write"

    # using --repo flag to uenv
    run uenv --repo=$RP repo status
    assert_success
    assert_line --index 0 "the repository at $RP is read-write"

    # as a positional argument to the repo status command itself
    run uenv repo status $RP
    assert_success
    assert_line --index 0 "the repository at $RP is read-write"

    # no error for a path that does not exist
    run uenv repo status /wombat
    assert_success
    assert_line --index 0 "no repository at /wombat"

    # TODO:
    # - check a read-only repo
    # - check an invalid repo
}

@test "repo create" {
    # using UENV_REPO_PATH env variable
    RP=$(mktemp -d $REPO_ROOT/create-XXXXXX)
    run uenv repo create $RP
    assert_success
    assert [ -d  $RP ]
    assert [ -e  $RP/index.db ]
    run sqlite3 $RP/index.db .dump
    assert_line --partial "CREATE TABLE images"
    assert_line --partial "CREATE TABLE uenv"
    assert_line --partial "CREATE TABLE tags"
    assert_line --partial "CREATE VIEW records AS"

    # create a repo in the same location
    # this should be an error
    run uenv repo create $RP
    assert_failure
    assert_line --partial "unable to create repository"

    # try to create a uenv in a read-only path
    RP=$REPO_ROOT/ro
    mkdir --mode=-w $RP
    run uenv repo create $RP/test
    assert_failure
    assert_line --partial "Permission denied"
}

@test "run" {
    export UENV_REPO_PATH=$REPOS/apptool
    export CLUSTER_NAME=arapiles

    #
    # check that run looks up images in the repo and mounts at the correct location
    #
    run uenv run tool -- ls /user-tools
    assert_success
    assert_output --regexp "meta"

    run uenv run app/42.0:v1 -- ls /user-environment
    assert_success
    assert_output --regexp "meta"

    #
    # check --view
    #
    run uenv run --view=tool tool -- tool
    assert_success
    assert_output "hello tool"

    run uenv run --view=app app/42.0:v1 -- app
    assert_success
    assert_output "hello app"

    #
    # check --view works when reading meta data from inside a standalone sqfs file
    #

    run uenv run --view=tool $SQFS_LIB/apptool/standalone/tool.squashfs -- tool
    assert_success
    assert_output "hello tool"
}

@test "image add" {
    # using UENV_REPO_PATH env variable
    RP=$(mktemp -d $REPO_ROOT/create-XXXXXX)
    run uenv repo create $RP
    assert_success

    run uenv --repo=$RP image add wombat/24:v1@arapiles%zen3 $SQFS_LIB/apptool/standalone/tool.squashfs
    assert_success
    assert_output --partial "the uenv wombat/24:v1@arapiles%zen3"
    assert_output --partial "was added to $RP"
    refute_output --partial "warning"
    refute_output --partial "error"

    # trying to add the same image should be a failure
    run uenv --repo=$RP image add wombat/24:v1@arapiles%zen3 $SQFS_LIB/apptool/standalone/tool.squashfs
    assert_failure
    assert_output --partial "uenv already exists"

    # trying to add the same image with a different label should add the image and generate a warning
    run uenv --repo=$RP image add numbat/24:v1@arapiles%zen3 $SQFS_LIB/apptool/standalone/tool.squashfs
    assert_success
    assert_output --partial "[warning] a uenv with the same sha"

    run uenv --repo=$RP image ls --no-header
    assert_success
    assert_line --partial 'wombat/24:v1'
    assert_line --partial 'numbat/24:v1'
    # assert that the number of lines in the output is 2: one for each uenv
    [ "${#lines[@]}" -eq "2" ]

    # trying to add the same image with a different label should add the image and generate a warning
    run uenv --repo=$RP image add bilby/24:v1@arapiles%zen3 $SQFS_LIB/apptool/standalone/app42.squashfs
    assert_success
    refute_output --partial "warning"
    refute_output --partial "error"

    run uenv --repo=$RP image ls --no-header
    assert_success
    assert_line --partial 'wombat/24:v1'
    assert_line --partial 'numbat/24:v1'
    assert_line --partial 'bilby/24:v1'
    [ "${#lines[@]}" -eq "3" ]

    # test that moving an image into place works
    # we have to move the image from the same file system as the repo
    # so we use a copy in REPO_ROOT/scratch
    sqfs_file=$REPO_ROOT/scratch/app43.squashfs
    cp $SQFS_LIB/apptool/standalone/app43.squashfs $sqfs_file
    [ -f  $sqfs_file ]
    run uenv --repo=$RP image add --move quokka/24:v1@arapiles%zen3 $sqfs_file
    assert_success
    refute_output --partial "warning"
    refute_output --partial "error"
    # assert that the input file was moved properly
    ! [ -f  $sqfs_file ]

    run uenv --repo=$RP image ls --no-header
    assert_success
    assert_line --partial 'wombat/24:v1'
    assert_line --partial 'numbat/24:v1'
    assert_line --partial 'bilby/24:v1'
    assert_line --partial 'quokka/24:v1'
    [ "${#lines[@]}" -eq "4" ]

    # TODO:
    # - check a read-only repo
}
