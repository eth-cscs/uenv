function setup() {
    bats_install_path=$(realpath ./install)
    export BATS_LIB_PATH=$bats_install_path/bats-helpers

    bats_load_library bats-support
    bats_load_library bats-assert
    load ./common

    export REPOS=$(realpath ../scratch/repos)
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
    # nothing is done if no --uenv is present and UENV_MOUNT_LIST is empty
    unset UENV_MOUNT_LIST
    export UENV_REPO_PATH=$REPOS/apptool

    run_srun_unchecked  --uenv=app bash -c 'findmnt -r | grep /user-environment'
    assert_output --partial '/user-environment'

    run_srun_unchecked  --uenv=app --view=app app
    assert_output --partial 'hello app'

    unset UENV_REPO_PATH
    run_srun_unchecked  --uenv=app --repo=$REPOS/apptool --view=app app
    assert_output --partial 'hello app'
    run_srun_unchecked  --uenv=app,tool --repo=$REPOS/apptool --view=app,tool tool
    assert_output --partial 'hello tool'
}

@test "faulty_argument" {
    export UENV_REPO_PATH=$REPOS/apptool

    run_srun_unchecked --uenv=a:b:c true
    assert_output --partial 'expected a path'

    run_srun_unchecked --uenv=a:/wombat true
    assert_output --partial "invalid uenv description: found unexpected '/'"

    run_srun_unchecked --uenv=a? true
    assert_output --partial "invalid uenv description: unexpected symbol '?'"

    run_srun_unchecked --uenv=app: true
    assert_output --partial 'invalid uenv description:'

    run_srun_unchecked --uenv=app/42.0:v1@arapiles%zen3+ ls /user-environment
    assert_output --partial "invalid uenv description: unexpected symbol '+'"
}
