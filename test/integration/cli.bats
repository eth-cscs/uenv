function setup() {
    # set the cluster name to be arapiles
    # this is required for tests to work when run on a vCluster
    # that sets this variable
    export CLUSTER_NAME=arapiles

    #echo "BATS_LIB_PATH $BATS_LIB_PATH" 1>&3
    bats_load_library bats-support
    bats_load_library bats-assert
    load ./common

    # TODO: set the BUILD_PATH from a template for out of tree builds
    export PATH="$BUILD_PATH:$PATH"

    unset UENV_MOUNT_LIST

    # set up location for creation of working repos
    export TMP=$DATA/scratch
    rm -rf $TMP
    mkdir -p $TMP

    # remove the bash function uenv, if an older version of uenv is installed on
    # the system
    unset -f uenv
}

function teardown() {
    :
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

    # test a label of the form "@system" with no name
    run uenv image ls --no-header @$CLUSTER_NAME
    assert_success
    refute_line --regexp "^uenv\s+arch\s+system\s+id"
    assert_line --regexp "app/42.0:v1\s+zen3\s+arapiles"
    assert_line --regexp "app/43.0:v1\s+zen3\s+arapiles"
    assert_line --regexp "tool/17.3.2:v1\s+zen3\s+arapiles"

    # test a label of the form "@'*'" with no name
    # this matches all clusters (overriding the default)
    run uenv image ls --no-header @'*'
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
    RP=$(mktemp -d $TMP/create-XXXXXX)
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
    RP=$TMP/ro
    mkdir --mode=-w $RP
    # run with logging to check for detailed "Permission denied" message
    run uenv -v repo create $RP/test
    assert_failure
    assert_line --partial "error: unable to create repository"
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
    # check --view and @system
    #
    run uenv run --view=tool tool@$CLUSTER_NAME -- tool
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

@test "start" {
    export UENV_REPO_PATH=$REPOS/apptool
    export CLUSTER_NAME=arapiles

    # set UENV_BATS_SKIP_START to skip the start tests.
    # used when running tests in a no-tty env (e.g. in GitHub action runner).
    [[ ! -z "$UENV_BATS_SKIP_START" ]] && skip

    #
    # check that run looks up images in the repo and mounts at the correct location
    #
    run uenv start tool <<EOF
echo 'hello world'
exit
EOF
    assert_failure
    assert_output --partial "must be run in an interactive shell"

    run uenv start --ignore-tty tool <<EOF
echo 'hello world'
exit
EOF
    assert_success
}

@test "image add" {
    # using UENV_REPO_PATH env variable
    RP=$(mktemp -d $TMP/create-XXXXXX)
    run uenv repo create $RP
    assert_success

    run uenv --repo=$RP image add wombat/24:v1@arapiles%zen3 $SQFS_LIB/apptool/standalone/tool.squashfs
    assert_success
    assert_output --partial "the uenv wombat/24:v1@arapiles%zen3"
    assert_output --partial "was added to $RP"
    refute_output --partial "warning"
    refute_output --partial "error"

    run uenv --repo=$RP image ls --no-header
    assert_success
    assert_output --partial "wombat/24:v1"

    # trying to add the same image should be a failure
    run uenv --repo=$RP image add wombat/24:v1@arapiles%zen3 $SQFS_LIB/apptool/standalone/tool.squashfs
    assert_failure
    assert_output --partial "uenv already exists"

    # trying to add the same image with a different label should add the image and generate a warning
    run uenv --repo=$RP image add numbat/24:v1@arapiles%zen3 $SQFS_LIB/apptool/standalone/tool.squashfs
    assert_success
    assert_output --partial "warning"
    assert_output --partial "a uenv with the same sha"

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
    # so we use a copy in TMP/scratch
    sqfs_file=$TMP/app43.squashfs
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

@test "image inspect" {
    export UENV_REPO_PATH=$REPOS/apptool
    export CLUSTER_NAME=arapiles

    run uenv image inspect tool
    assert_success

    run uenv image inspect --format='{name}:{tag}' tool
    assert_success
    assert_output "tool:v1"

    # check a format string that contains no fields
    run uenv image inspect --format='hello world' tool
    assert_success
    assert_output "hello world"

    run uenv image inspect --format='{sha256}' tool
    assert_success
    run uenv image inspect --format='{sqfs}' tool
    assert_success

    # get the sha and path of the tool squashfs image
    sha=$(uenv image inspect --format='{sha256}' tool)
    sqfs=$(uenv image inspect --format='{sqfs}' tool)

    # check that the squashfs file exists
    [ -f $sqfs ]

    # compute the actual sha and validate that it matches the one
    # returned by inspect
    computed_sha=$(sha256sum ${sqfs} | awk '{print $1}')
    [ "$computed_sha" == "$sha" ]
}

@test "image rm" {
    # using UENV_REPO_PATH env variable
    export UENV_REPO_PATH=$(mktemp -d $TMP/create-XXXXXX)
    run uenv repo create $UENV_REPO_PATH
    assert_success

    # add uenv to a repo for us to try removing
    # the wombat uenv have the same sha
    uenv image add wombat/24:rc1@arapiles%zen3 $SQFS_LIB/apptool/standalone/tool.squashfs > /dev/null
    uenv image add wombat/24:v1@arapiles%zen3  $SQFS_LIB/apptool/standalone/tool.squashfs > /dev/null
    # the bilby images have unique sha
    uenv image add bilby/24:v1@arapiles%zen3   $SQFS_LIB/apptool/standalone/app42.squashfs > /dev/null
    uenv image add bilby/24:v2@arapiles%zen3   $SQFS_LIB/apptool/standalone/app43.squashfs > /dev/null

    # test that removing an uenv that is not in the repo is handled correctly
    for pattern in dingo/24:rc1@arapiles%zen3  aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa aaaaaaaaaaaaaaaa
    do
        run uenv image rm $pattern
        assert_failure
        assert_output "error: no uenv matches $pattern"
    done

    # test that removing a sha with more than one label removes both labels
    sha=$(uenv image inspect wombat/24:rc1 --format={sha256})

    # assert that the files exist
    [ -d $UENV_REPO_PATH/images/$sha ]

    run uenv image rm $sha
    assert_success
    assert_output --partial "the following uenv were removed"
    assert_output --partial "wombat/24:rc1"
    assert_output --partial "wombat/24:v1"

    # verify that the sha and wombat image are no longer in the repo
    run uenv image ls $sha
    assert_output 'no matching uenv'
    run uenv image ls wombat
    assert_output 'no matching uenv'

    # verify that the file was removed
    [ ! -d $UENV_REPO_PATH/images/$sha ]

    # remove using a label that is the only label attached to a sha
    # - should remove the label
    # - should remove the storage
    pattern=bilby/24:v1
    sha=$(uenv image inspect $pattern --format={sha256})
    [ -d $UENV_REPO_PATH/images/$sha ]
    run uenv -vv image rm $pattern
    assert_success
    # verify that the file was removed
    [ ! -d $UENV_REPO_PATH/images/$sha ]
    # check that the uenv was removed from database
    run uenv image ls $pattern
    assert_output "no matching uenv"

    # removing a one of multiple labels on the same sha removes the label but leaves the others untouched
    # step 1: add another image with the same hash as the remaining image
    run uenv image add wallaby/24:v2@arapiles%zen3   $SQFS_LIB/apptool/standalone/app43.squashfs > /dev/null
    run uenv image add wallaby/24:v3@arapiles%zen3   $SQFS_LIB/apptool/standalone/app43.squashfs > /dev/null

    pattern=wallaby/24:v2
    sha=$(uenv image inspect $pattern --format={sha256})
    [ -d $UENV_REPO_PATH/images/$sha ]

    run uenv -vv image rm $pattern
    assert_success
    [ -d $UENV_REPO_PATH/images/$sha ]

    pattern=wallaby/24:v3
    run uenv image rm $pattern
    assert_success
    [ -d $UENV_REPO_PATH/images/$sha ]

    pattern=bilby/24:v2
    run uenv image rm $pattern
    assert_success
    [ ! -d $UENV_REPO_PATH/images/$sha ]
}

