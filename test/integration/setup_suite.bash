# this file is automatically detected by bats
# it performs setup and teardown to run once, before and after respectively, all of the tests are run.

# create the input data if it has not already been created.
function setup_suite() {
    DIR="$( cd "$( dirname "$BATS_TEST_FILENAME" )" >/dev/null 2>&1 && pwd )"
    scratch=$DIR/../scratch

    if [ ! -d $scratch ]; then
        $DIR/../setup/setup $scratch
    fi
    export REPOS=$(realpath ${scratch}/repos)
}

function teardown_suite() {
    :
}

