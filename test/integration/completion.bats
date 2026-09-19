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
    # registry labels are completed from the listings cached here, not from
    # those in the user's home
    export XDG_CACHE_HOME=$TMP/cache

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

@test "~ in the words before the cursor is expanded" {
    # the shell passes the words to completion as they were typed: uenv must
    # expand ~ as the shell will when the command is run
    mkdir -p $TMP/home
    cp -r $REPO $TMP/home/repo
    export HOME=$TMP/home

    complete_line uenv --repo '~/repo' image ls ""
    assert_line app/42.0:v1

    complete_word 4 uenv --repo '~/repo/' start --view= tool
    assert_line --view=wombat

    # the word at the cursor keeps its ~
    complete_line uenv --repo '~/re'
    assert_output "~/repo/
:filenames,nospace"
}

@test "variables in the words are expanded" {
    export MYREPOS=$TMP/repos
    mkdir -p $MYREPOS
    cp -r $REPO $MYREPOS/apptool

    complete_line uenv '--repo=$MYREPOS/apptool' image ls ""
    assert_line app/42.0:v1

    complete_line uenv --repo '"${MYREPOS}"/apptool' start ""
    assert_line app/42.0:v1

    # the word at the cursor keeps the variable: bash must not quote its $
    complete_line uenv --repo '$MYREPOS/a'
    assert_output '$MYREPOS/apptool/
:nospace'
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

# Serve a listing of the registry, with a record for each of the paths given
# as arguments, and configure uenv to use it. The server is stopped with
# stop_listing.
function serve_listing() {
    LISTING=$TMP/listing.jsonl
    local path sha
    for path in "$@"; do
        sha=$(printf '%s' "$path" | sha256sum | cut -c1-64)
        listing_mock add $LISTING --path $path --sha $sha >/dev/null
    done
    local port=$(listing_mock free-port)
    listing_mock serve $LISTING $port &
    listing_mock wait-server $port --timeout 5
    cat >> $XDG_CONFIG_HOME/uenv/config.toml <<EOF
[registry]
url = "http://127.0.0.1:1/uenv"
default_namespace = "deploy"
listing_url = "http://127.0.0.1:$port/list"
EOF
}

function stop_listing() {
    listing_mock kill $LISTING 2>/dev/null || true
}

function teardown() {
    if [[ -n "${LISTING:-}" ]]; then
        stop_listing
    fi
}

# The number of requests for a namespace that the listing service has served.
function requests() {
    listing_mock requests $LISTING --namespace $1
}

# Complete the last word of the command line given by the arguments until
# `value`, the first argument, is offered, for at most 5 seconds: a cached
# listing is refreshed by a process that completion does not wait for.
function wait_for_candidate() {
    local value=$1
    shift
    local i
    for i in $(seq 50); do
        complete_line "$@"
        if printf '%s\n' "${lines[@]}" | grep -qxF -- "$value"; then
            return 0
        fi
        sleep 0.1
    done
    fail "'$value' was not offered for: $*"
}

# complete_line, which must take less than a second
function quick_complete_line() {
    local start=$(date +%s%N)
    complete_line "$@"
    local ms=$(( ($(date +%s%N) - start) / 1000000 ))
    (( ms < 1000 )) || fail "completion took ${ms} ms: $*"
}

# The cached listing of a namespace, and the stamp of its last refresh.
function cached_listing() {
    echo $(find $XDG_CACHE_HOME/uenv/listing -name $1.json)
}
function refresh_stamp() {
    echo $(find $XDG_CACHE_HOME/uenv/listing -name .$1.refresh)
}

@test "registry labels are completed from the cached listings" {
    serve_listing \
        deploy/arapiles/zen3/prgenv-gnu/24.11/v1 \
        deploy/arapiles/zen3/prgenv-gnu/24.11/v2 \
        deploy/arapiles/zen3/netcdf/4.9/v1 \
        deploy/eiger/zen2/prgenv-gnu/24.11/v3 \
        build/arapiles/zen3/prgenv-gnu/24.11/1551223269 \
        service/arapiles/zen3/tool/1.0/v1 \
        mine/arapiles/zen3/tool/2.0/v1

    # nothing is cached: only the namespaces are known
    complete_line uenv image delete ""
    assert_output "build::
deploy::
service::
:nospace"
    complete_line uenv image pull ""
    assert_output ":"
    # which started a refresh of the default namespace
    wait_for_candidate prgenv-gnu/24.11:v1 uenv image pull ""

    # every command that fetches a listing caches it
    run uenv image find build::
    assert_success
    run uenv image find mine::
    assert_success
    # completion reads only the cache
    stop_listing

    # pull and find: the default namespace, then the others
    complete_line uenv image pull ""
    assert_output "netcdf/4.9:v1
prgenv-gnu/24.11:v1
prgenv-gnu/24.11:v2
:"
    complete_line uenv image find prgenv-gnu/24.11:v
    assert_output "prgenv-gnu/24.11:v1
prgenv-gnu/24.11:v2
:"
    complete_line uenv image pull prgenv-gnu/24.11:v3@
    assert_output "prgenv-gnu/24.11:v3@eiger
:"
    complete_line uenv image pull b
    assert_output "build::
:nospace"
    complete_line uenv image pull build::
    assert_output "build::prgenv-gnu/24.11:1551223269
:"
    # a namespace that is not the site's is known from the cache
    complete_line uenv image find m
    assert_output "mine::
:nospace"
    complete_line uenv image find mine::
    assert_output "mine::tool/2.0:v1
:"
    # a namespace that has not been cached
    complete_line uenv image find service::
    assert_output ":"

    # delete and the source of copy: the namespace first
    complete_line uenv image delete ""
    assert_output "build::
deploy::
mine::
service::
:nospace"
    complete_line uenv image copy build::p
    assert_output "build::prgenv-gnu/24.11:1551223269
:"

    # the destination of copy: the name and version of the source
    complete_line uenv image copy build::prgenv-gnu/24.11:1551223269 d
    assert_output "deploy::
:nospace"
    complete_line uenv image copy build::prgenv-gnu/24.11:1551223269 deploy::
    assert_output "deploy::prgenv-gnu/24.11:
:nospace"
    complete_line uenv image copy build::nothing deploy::
    assert_output ":"

    # the destination of push: the name and version of the local source, then
    # its system and uarch
    complete_line uenv --repo=$REPO image push app/42.0:v1 deploy::
    assert_output "deploy::app/42.0:
:nospace"
    complete_line uenv --repo=$REPO image push app/42.0:v1 deploy::app/42.0:v2@
    assert_output --regexp "^deploy::app/42.0:v2@arapiles%[a-z0-9]+
:$"

    # an old listing is not used
    touch -d "40 days ago" $(cached_listing deploy)
    complete_line uenv image pull ""
    assert_output ":"
}

@test "cached registry listings are refreshed in the background" {
    serve_listing deploy/arapiles/zen3/prgenv-gnu/24.11/v1

    # the first completion starts a refresh, and a later one uses it
    complete_line uenv image pull ""
    assert_output ":"
    wait_for_candidate prgenv-gnu/24.11:v1 uenv image pull ""
    assert_equal "$(requests deploy)" 1

    # a fresh listing is not refreshed
    complete_line uenv image pull ""
    complete_line uenv image find ""
    assert_equal "$(requests deploy)" 1

    # a stale listing is used while it is refreshed
    listing_mock add $LISTING --path deploy/arapiles/zen3/prgenv-gnu/24.11/v2 \
        --sha $(printf v2 | sha256sum | cut -c1-64)
    touch -d "2 minutes ago" $(cached_listing deploy) $(refresh_stamp deploy)
    complete_line uenv image pull ""
    assert_output "prgenv-gnu/24.11:v1
:"
    wait_for_candidate prgenv-gnu/24.11:v2 uenv image pull ""
    assert_equal "$(requests deploy)" 2

    # at most one refresh per minute, even if the listing is stale
    touch -d "2 minutes ago" $(cached_listing deploy)
    complete_line uenv image pull ""
    complete_line uenv image pull ""
    sleep 0.5
    assert_equal "$(requests deploy)" 2

    # a namespace that is only typed is not fetched
    complete_line uenv image pull typo::
    complete_line uenv image delete typo::
    sleep 0.5
    assert_equal "$(requests typo)" 0

    # the site's namespaces are
    complete_line uenv image delete service::
    for i in $(seq 50); do
        (( $(requests service) == 1 )) && break
        sleep 0.1
    done
    assert_equal "$(requests service)" 1
}

@test "a broken listing cache never holds up completion" {
    serve_listing deploy/arapiles/zen3/prgenv-gnu/24.11/v1
    run uenv image find
    assert_success
    local listing=$(cached_listing deploy)
    local dir=$(dirname $listing)
    local stamp=$dir/.deploy.refresh

    # every state of the listing is either used or replaced
    local state
    for state in garbage truncated large fifo directory; do
        rm -rf $listing
        case $state in
        garbage) echo "garbage" > $listing ;;
        truncated) head -c 20 $TMP/listing.jsonl > $listing ;;
        large) truncate -s 20M $listing ;;
        fifo) mkfifo $listing ;;
        directory) mkdir -p $listing/sub ;;
        esac
        # a refresh has not been claimed for the last minute
        rm -rf $stamp
        quick_complete_line uenv image pull ""
        assert_output ":"
        wait_for_candidate prgenv-gnu/24.11:v1 uenv image pull ""
        [[ -f $listing ]]
    done

    # a temporary file left by a writer that was killed is removed by the next
    # refresh
    echo garbage > $dir/.deploy.json.12345
    touch -d "2 hours ago" $dir/.deploy.json.12345
    touch -d "2 minutes ago" $listing $stamp
    complete_line uenv image pull ""
    assert_line prgenv-gnu/24.11:v1
    for i in $(seq 50); do
        [[ ! -e $dir/.deploy.json.12345 ]] && break
        sleep 0.1
    done
    [[ ! -e $dir/.deploy.json.12345 ]]

    # a cache that can't be written: the listing is used, and not refreshed
    touch -d "2 minutes ago" $listing $stamp
    local before=$(requests deploy)
    chmod a-w $dir
    if [[ -w $dir ]]; then
        chmod u+w $dir
        skip "root can write to a read-only directory"
    fi
    quick_complete_line uenv image pull ""
    chmod u+w $dir
    assert_line prgenv-gnu/24.11:v1
    sleep 0.5
    assert_equal "$(requests deploy)" "$before"
}

@test "a listing service that does not answer never holds up completion" {
    LISTING=$TMP/listing.jsonl
    local port=$(listing_mock free-port)
    listing_mock serve --hang $LISTING $port &
    listing_mock wait-server $port --timeout 5
    cat >> $XDG_CONFIG_HOME/uenv/config.toml <<EOF
[registry]
url = "http://127.0.0.1:1/uenv"
default_namespace = "deploy"
listing_url = "http://127.0.0.1:$port/list"
EOF

    quick_complete_line uenv image pull ""
    assert_output ":"
    # the refresh is waiting for an answer
    for i in $(seq 50); do
        (( $(requests deploy) == 1 )) && break
        sleep 0.1
    done
    assert_equal "$(requests deploy)" 1
    quick_complete_line uenv image pull ""
    assert_output ":"
    quick_complete_line uenv image delete deploy::
    assert_output ":"
    stop_listing
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

# Complete the command line $1 with the bash completion script printed by
# `uenv completion bash`, as bash would when TAB is pressed at the end of the
# line. If $2 is "with", bash-completion is loaded first. On return `lines`
# holds COMPREPLY, followed by a line with the options passed to compopt.
function bash_complete() {
    run bash --norc --noprofile -c '
        [[ $2 == with ]] && source /usr/share/bash-completion/bash_completion
        source <(uenv completion bash)
        compopt() { opts+=("$@"); }
        # split the line into COMP_WORDS as readline does: at white space and
        # at the word break characters = : and @
        line=$1
        COMP_LINE=$line
        COMP_POINT=${#line}
        COMP_WORDS=()
        for w in $line; do
            while [[ -n $w ]]; do
                if [[ $w == [=:@]* ]]; then
                    b=${w%%[^=:@]*}
                else
                    b=${w%%[=:@]*}
                fi
                COMP_WORDS+=("$b")
                w=${w:${#b}}
            done
        done
        [[ $line == *" " ]] && COMP_WORDS+=("")
        COMP_CWORD=$((${#COMP_WORDS[@]} - 1))
        opts=()
        _uenv_complete uenv
        printf "%s\n" "${COMPREPLY[@]}"
        echo "compopt: ${opts[*]}"
    ' bash "$@"
    assert_success
}

@test "bash completion script" {
    bash_complete "uenv st"
    assert_output "start
status
compopt: "

    # candidates are what replaces the text after the last : = or @
    bash_complete "uenv --repo=$REPO start app/42.0:"
    assert_output "v1
compopt: "

    bash_complete "uenv --repo=$REPO start app/42.0:v1@a"
    assert_output "@arapiles
compopt: "

    bash_complete "uenv --repo=$REPO start tool --view=w"
    assert_output "wombat
compopt: "

    bash_complete "uenv --repo=$REPO run app/42.0:v1,tool -v tool:w"
    assert_output "wombat
compopt: "

    # file names: readline adds the / to directories
    bash_complete "uenv --repo=$REPO start $SQFS_LIB/apptool/to"
    assert_output "$SQFS_LIB/apptool/tool
compopt: -o filenames"

    bash_complete "uenv --repo=$REPO run app/42.0:v1,$SQFS_LIB/apptool/to"
    assert_output "v1,$SQFS_LIB/apptool/tool/
compopt: -o nospace"

    # the command run by uenv run, without bash-completion
    bash_complete "uenv --repo=$REPO run tool -- ech"
    assert_line echo
}

@test "bash completion script with bash-completion" {
    [[ -f /usr/share/bash-completion/bash_completion ]] || skip "bash-completion is not installed"

    bash_complete "uenv --repo=$REPO start app/42.0:" with
    assert_output "v1
compopt: "

    bash_complete "uenv --repo=$REPO run tool -- ech" with
    assert_line echo
}

@test "zsh completion script" {
    command -v zsh >/dev/null || skip "zsh is not installed"

    # the completion system's functions are replaced by ones that print what
    # they are given
    function zsh_complete() {
        run zsh -f -c '
            compdef() { }
            _describe() { print -rl -- "${(@P)4}"; print -r -- "opts: ${@:5}" }
            _normal() { print -r -- "normal: $CURRENT ${words[*]}" }
            source <(uenv completion zsh)
            words=("$@")
            CURRENT=${#words}
            PREFIX=${words[-1]}
            _uenv
        ' zsh "$@"
        assert_success
    }

    zsh_complete uenv st
    assert_output "start:start a uenv session
status:print information about the currently loaded uenv
opts: "

    # colons in values are escaped
    zsh_complete uenv --repo=$REPO start app/42
    assert_output "app/42.0\:v1:@arapiles%zen3
opts: "

    zsh_complete uenv --repo=$REPO start $SQFS_LIB/apptool/to
    assert_output "$SQFS_LIB/apptool/tool/
opts: -f -S "

    # the command run by uenv run: the words from the command on
    zsh_complete uenv --repo=$REPO run tool -- ech
    assert_output "normal: 1 ech"
}
