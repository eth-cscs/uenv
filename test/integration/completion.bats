bats_require_minimum_version 1.5.0

function setup() {
    set -u
    export USER="${USER:-$(whoami)}"
    export CLUSTER_NAME=arapiles

    bats_load_library bats-support
    bats_load_library bats-assert
    load ./common

    export PATH="$UENV_BIN_PATH:$PATH"

    export TMP=$DATA/scratch
    rm -rf $TMP
    mkdir -p $TMP

    # see cli.bats: force the system name to arapiles
    export XDG_CONFIG_HOME=$TMP/user-config
    mkdir -p $XDG_CONFIG_HOME/uenv
    cat > $XDG_CONFIG_HOME/uenv/config.toml <<EOF
system_name = 'arapiles'
EOF

    export REPO=$REPOS/apptool

    unset -f uenv
}

# Complete word `cword` of the command line given by the remaining arguments.
# On return `output` and `lines` hold the values of the candidates, one per
# line, followed by the line of directives. Completion must succeed, and must
# not write anything to stderr.
function complete_word() {
    local cword=$1
    shift
    run --separate-stderr uenv __complete --cword=$cword -- "$@"
    assert_success
    assert_equal "$stderr" ""
    output=$(printf '%s\n' "${lines[@]}" | cut -f1)
    mapfile -t lines <<< "$output"
}

# complete the last word of the command line
function complete_line() {
    complete_word $(( $# - 1 )) "$@"
}

function directives() {
    assert_equal "${lines[-1]}" "$1"
}

@test "subcommands" {
    complete_line uenv ""
    assert_line start
    assert_line run
    assert_line image
    assert_line completion
    directives ":"

    complete_line uenv st
    assert_output "start
status
:"

    complete_line uenv image r
    assert_output "rm
:"

    complete_line uenv repo ""
    assert_line create
    assert_line migrate
}

@test "the descriptions of subcommands and options" {
    run --separate-stderr uenv __complete --cword=1 -- uenv sta
    assert_output "$(printf 'start\tstart a uenv session\nstatus\tprint information about the currently loaded uenv\n:')"
}

@test "options, long and short, per command" {
    complete_line uenv -
    assert_line --verbose
    assert_line -v
    assert_line --no-color
    refute_line --view

    complete_line uenv run -
    assert_line --view
    assert_line -v
    assert_line --no-default-view
    refute_line --verbose

    complete_line uenv run --
    assert_line --view
    refute_line -v

    complete_line uenv image ls --no
    assert_output "--no-header
--no-partials
:"
}

@test "options already given are not offered again" {
    complete_line uenv start --view=x --
    refute_line --view
    assert_line --help

    # also when they come after the cursor
    complete_word 2 uenv start -- --view=x
    refute_line --view

    # -v is a counter at the top level, so it can be repeated
    complete_line uenv -v -
    assert_line -v
}

@test "option values" {
    complete_line uenv status --format ""
    assert_output "short
full
views
:"

    complete_line uenv status --format=f
    assert_output "--format=full
:"

    complete_line uenv --repo=$REPO --system ""
    assert_output "arapiles
:"

    # free text: nothing to offer
    complete_line uenv image ls --format ""
    assert_output ":"
}

@test "positionals are completed by position" {
    complete_line uenv --repo=$REPO inspect ""
    assert_line app/42.0:v1

    # the uenv has been given: only options are left
    complete_line uenv --repo=$REPO inspect tool ""
    refute_line app/42.0:v1
    assert_line --json

    # the label and then the squashfs file of image add
    complete_line uenv --repo=$REPO image add ""
    assert_output ":"
    complete_line uenv --repo=$REPO image add app/1.0:v1 $SQFS_LIB/apptool/tool/
    assert_output "$SQFS_LIB/apptool/tool/meta/
$SQFS_LIB/apptool/tool/store.squashfs
:filenames,nospace"

    complete_line uenv completion ""
    assert_output "bash
zsh
:"
}

@test "labels" {
    complete_line uenv --repo=$REPO start ""
    assert_output "app/42.0:v1
app/43.0:v1
tool/17.3.2:v1
:"

    complete_line uenv --repo=$REPO run app/4
    assert_output "app/42.0:v1
app/43.0:v1
:"

    complete_line uenv --repo=$REPO start app/42.0:v1@
    assert_output "app/42.0:v1@arapiles
:"

    complete_line uenv --repo=$REPO start tool/17.3.2:v1%
    assert_output "tool/17.3.2:v1%zen3
:"

    complete_line uenv --repo=$REPO image ls t
    assert_output "tool/17.3.2:v1
:"

    complete_line uenv --repo=$REPO image rm a
    assert_line app/42.0:v1

    # the labels are those of the system: there are none on daint
    complete_line uenv --repo=$REPO --system=daint start ""
    refute_line app/42.0:v1
}

@test "lists of uenvs" {
    complete_line uenv --repo=$REPO start tool,app/43
    assert_output "tool,app/43.0:v1
:"

    complete_line uenv --repo=$REPO run app/42.0:v1,$SQFS_LIB/apptool/to
    assert_output "app/42.0:v1,$SQFS_LIB/apptool/tool/
:nospace"

    # the mount point after a label
    mkdir -p $TMP/mnt/point
    complete_line uenv --repo=$REPO start tool:$TMP/mnt/p
    assert_output "tool:$TMP/mnt/point/
:nospace"
}

@test "squashfs files" {
    complete_line uenv --repo=$REPO start $SQFS_LIB/apptool/standalone/
    assert_line $SQFS_LIB/apptool/standalone/app42.squashfs
    directives ":filenames"

    cd $SQFS_LIB/apptool/tool
    complete_line uenv --repo=$REPO start ./
    assert_output "./meta/
./store.squashfs
:filenames,nospace"
}

@test "the repository is taken from the command line" {
    uenv repo create $TMP/empty

    complete_line uenv --repo=$TMP/empty start ""
    refute_line app/42.0:v1

    complete_line uenv --repo=$REPO start ""
    assert_line app/42.0:v1

    complete_line uenv --repo $TMP/em
    assert_output "$TMP/empty/
:filenames,nospace"
}

@test "views of the uenvs on the command line" {
    # the uenv comes after the cursor
    complete_word 3 uenv --repo=$REPO start --view= tool
    assert_output "--view=defaultwombat
--view=modules
--view=spack
--view=tool
--view=wombat
:"

    complete_word 4 uenv --repo=$REPO start --view "w" tool
    assert_output "wombat
:"

    complete_word 3 uenv --repo=$REPO run -vmo tool
    assert_output "-vmodules
:"

    # views of more than one uenv are qualified with the name of the uenv
    complete_line uenv --repo=$REPO run app/42.0:v1,tool -v tool:w
    assert_output "tool:wombat
:"

    complete_line uenv --repo=$REPO run app/42.0:v1 -v app,
    assert_output "app,app
app,modules
app,spack
:"

    # a squashfs file with its meta data next to it
    complete_word 2 uenv start --view= $SQFS_LIB/apptool/tool/store.squashfs
    assert_line --view=wombat

    # there is no uenv to take views from
    complete_line uenv --repo=$REPO start --view ""
    assert_output ":"
}

@test "the command run by uenv run" {
    complete_line uenv --repo=$REPO run tool ""
    assert_output ":command-offset=4"

    complete_line uenv --repo=$REPO run tool ls -
    assert_output ":command-offset=4"

    complete_line uenv --repo=$REPO run tool -- ""
    assert_output ":command-offset=5"
}

@test "registry labels are not completed" {
    complete_line uenv image pull ""
    assert_output ":"
}

@test "completion is silent and does not create files" {
    # the user configuration is not created
    export XDG_CONFIG_HOME=$TMP/no-config
    complete_line uenv --repo=$REPO start ""
    assert_line app/42.0:v1
    [ ! -e $TMP/no-config ]

    # a broken configuration file
    export XDG_CONFIG_HOME=$TMP/bad-config
    mkdir -p $XDG_CONFIG_HOME/uenv
    echo "this is not [ toml" > $XDG_CONFIG_HOME/uenv/config.toml
    complete_line uenv --repo=$REPO start ""

    # an invalid --repo
    complete_line uenv --repo=, start ""
    refute_line app/42.0:v1
}

@test "invalid requests" {
    run --separate-stderr uenv __complete
    assert_success
    assert_output ":"
    assert_equal "$stderr" ""

    run --separate-stderr uenv __complete --cword=x -- uenv ""
    assert_success
    assert_output ":"

    # the program name is not completed
    run --separate-stderr uenv __complete --cword=0 -- uenv
    assert_success
    assert_output ":"

    # a cursor past the end of the line completes an empty word
    run --separate-stderr uenv __complete --cword=9 -- uenv st
    assert_success
    assert_line --regexp "^start\s"
}

@test "__complete is not a command" {
    run uenv --help
    refute_output --partial __complete
}
