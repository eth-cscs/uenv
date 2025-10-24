function setup() {
    # set the cluster name to be arapiles
    # this is required for tests to work when run on a vCluster
    # that sets this variable
    set -u
    export CLUSTER_NAME=arapiles

    bats_load_library bats-support
    bats_load_library bats-assert
    load ./common

    unset UENV_MOUNT_LIST

    export PATH="$UENV_BIN_PATH:$PATH"

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
    # nothing is done if no --uenv is present and UENV_MOUNT_LIST is empty
    unset UENV_MOUNT_LIST
    run_srun_unchecked  bash -c 'findmnt -r | grep /user-environment'
    refute_output --partial '/user-environment'
}

@test "mount uenv" {
    export RP=$REPOS/apptool

    # if no mount point is provided the default provided by the uenv's meta
    # data should be used.

    # app has default mount /user-environment
    run_srun_unchecked --repo=$RP  --uenv=app/42.0 bash -c 'findmnt -r | grep /user-environment'
    assert_output --partial '/user-environment'

    SLURM_UENV=app/42.0 run_srun_unchecked  --repo=$RP bash -c 'findmnt -r | grep /user-environment'
    assert_output --partial '/user-environment'

    # tool has default mount /user-tools
    run_srun_unchecked --repo=$RP  --uenv=tool bash -c 'findmnt -r | grep /user-tools'
    assert_output --partial '/user-tools'

    # if the view is mounted, the app should be visible
    run_srun_unchecked --repo=$RP --uenv=app/42.0 --view=app app
    assert_output --partial 'hello app'

    # check SLURM_UENV, SLURM_UENV_VIEW env variables
    # if the view is mounted, the app should be visible
    SLURM_UENV=app/42.0 SLURM_UENV_VIEW=app run_srun_unchecked --repo=$RP app
    assert_output --partial 'hello app'

    # if the view is mounted, the app should be visible
    run_srun_unchecked --repo=$RP --uenv=tool --view=tool tool
    assert_output --partial 'hello tool'

    # check that the correct uenv with name app is chosen
    run_srun_unchecked --repo=$RP --uenv=app/43.0 --view=app app --version
    assert_output --partial '43.0'
    run_srun_unchecked --repo=$RP --uenv=app/42.0 --view=app app --version
    assert_output --partial '42.0'

    # an error should be generated if an ambiguous uenv is requested
    run_srun_unchecked --repo=$RP  --uenv=app --view=app app --version
    assert_output --partial "error: more than one uenv matches the uenv description 'app'"

    run_srun_unchecked  --uenv=app/43.0 --repo=$REPOS/apptool --view=app app
    assert_output --partial 'hello app'
    run_srun_unchecked  --uenv=app/42.0:v1@arapiles%zen3,tool --repo=$REPOS/apptool --view=app,tool tool
    assert_output --partial 'hello tool'
}

@test "views" {
    export RP=$REPOS/apptool

    # if the view is mounted, the app should be visible
    run_srun_unchecked --repo=$RP --uenv=app/42.0 --view=app app
    assert_output --partial 'hello app'

    # if the view is mounted, the app should be visible
    run_srun_unchecked --repo=$RP --uenv=tool --view=tool tool
    assert_output --partial 'hello tool'

    # check multiple views + uenv
    run_srun_unchecked --repo=$RP --uenv=app/42.0,tool --view=app,tool bash -c "tool; app"
    assert_output --partial 'hello tool'
    assert_output --partial 'hello app'
    run_srun_unchecked --repo=$RP --uenv=app/42.0,tool --view=app:app,tool:tool bash -c "tool; app"
    assert_output --partial 'hello tool'
    assert_output --partial 'hello app'

    # check that invalid view names are caught
    run_srun_unchecked --repo=$RP --uenv=tool --view=tools true
    assert_output --partial "the view 'tools' does not exist"
    run_srun_unchecked --repo=$RP --uenv=app/42.0,tool --view=app:app,tool:tools true
    assert_output --partial "the view 'tools' does not exist"
    run_srun_unchecked --repo=$RP --uenv=app/42.0,tool --view=app:app,wombat:tool true
    assert_output --partial "the view 'wombat:tool' does not exist"

    # the tool view in the tool environment:
    # - sets TOOLVAR=SET
    # - unsets TOOLCONFLICT
    # check that this is the case
    export TOOLCONFLICT="yes"
    export TOOLCONFLICTS="yes"
    unset TOOLVAR
    run_srun_unchecked --repo=$REPOS/apptool --view=tool --uenv=tool -- printenv
    assert_success
    assert_line "TOOLVAR=SET"
    assert_line "TOOLCONFLICTS=yes"
    refute_line "TOOLCONFLICT=yes"
    unset TOOLCONFLICT
}

@test "fwd_non_posix_envvars" {
    export RP=$REPOS/apptool

    # check that exported bash functions (which have non-posix names) are forwarded correctly
    hello() { echo "hello world $1"; }
    export -f hello
    run_srun_unchecked --repo=$RP --uenv=app/42.0 --view=app bash -c 'hello hpc'
    assert_output "hello world hpc"
}

# check for invalid arguments passed to --uenv
@test "faulty --uenv argument" {
    export RP=$REPOS/apptool

    run_srun_unchecked --uenv=a:b:c true
    assert_output --partial 'expected a path'

    run_srun_unchecked --uenv=a? true
    assert_output --partial "invalid uenv description: unexpected symbol '?'"

    run_srun_unchecked --uenv=app: true
    assert_output --partial 'invalid uenv description:'

    run_srun_unchecked --uenv=app/42.0:v1@arapiles%zen3+ ls /user-environment
    assert_output --partial "invalid uenv description: unexpected symbol '+'"
}

@test "custom mount point" {
    export RP=$REPOS/apptool

    run_srun_unchecked --repo=$RP --uenv=app/42.0:/user-environment bash -c 'findmnt -r | grep /user-environment'
    assert_output --partial "/user-environment"
}

@test "duplicate mount fails" {
    export RP=$REPOS/apptool

    # duplicate images fail
    run_srun_unchecked  --repo=$RP --uenv=tool:/user-environment,app/42.0:/user-environment true
    assert_output --partial "more than one image mounted at the mount point '/user-environment'"
}

@test "duplicate image fails" {
    export RP=$REPOS/apptool

    # duplicate images fail
    run_srun_unchecked --repo=$RP --uenv=tool:/user-environment,tool true
    assert_output --partial "uenv is mounted more than once"
}

@test "empty --uenv argument" {
    run_srun_unchecked --repo=$REPOS/apptool --uenv='' true
    assert_output --partial 'invalid uenv description'
}

@test "sbatch" {
    run_sbatch <<EOF
#!/bin/bash
#SBATCH --uenv=app/42.0,tool
#SBATCH --repo=$REPOS/apptool
set -e
srun findmnt /user-environment
srun findmnt /user-tools
EOF
}

@test "sbatch override in srun" {
    # check that images mounted via sbatch --uenv are overriden when `--uenv` flag is given to srun
    run_sbatch <<EOF
#!/bin/bash
#SBATCH --uenv=app/42.0
#SBATCH --repo=$REPOS/apptool

set -e

# override --uenv and mount under /user-tools instead
srun --repo=$REPOS/apptool --uenv=tool findmnt /user-tools

# override, /user-environment must not be mounted
srun --repo=$REPOS/apptool --uenv=tool bash -c '! findmnt /user-environment'
EOF
}

@test "uenv start in sbatch should fail" {
    export UENV_REPO_PATH=$REPOS/apptool
    # check that images mounted via sbatch --uenv are overriden when `--uenv` flag is given to srun
    run run_sbatch <<EOF
#!/bin/bash

set -e

uenv --repo=$UENV_REPO_PATH start app/42.0
EOF
    [ "${status}" -eq "1" ]
    assert_output --partial "'uenv start' must be run in an interactive shell"
}

@test "sbatch UENV_MOUNT_LIST with no --uenv" {
    # this should be independent of the repo
    unset UENV_REPO_PATH

    export UENV_MOUNT_LIST=$SQFS_LIB/apptool/tool/store.squashfs:/user-tools
    run_sbatch <<EOF
#!/bin/bash
set -e
srun findmnt /user-tools
srun bash -c '! findmnt /user-environment'
EOF

    # sbatch should error if sqfs does not exist (there is a typo "toool" in sqfs name)
    export UENV_MOUNT_LIST=$SQFS_LIB/apptool/toool/store.squashfs:/user-tools
    run run_sbatch <<EOF
#!/bin/bash
echo "should not get here"
EOF
    [ "${status}" -eq "1" ]

    # sbatch should error if mount does not exist
    export UENV_MOUNT_LIST=$SQFS_LIB/apptool/tool/store.squashfs:/user-toooools
    run run_sbatch <<EOF
#!/bin/bash
echo "should not get here"
EOF
    [ "${status}" -eq "1" ]
}

