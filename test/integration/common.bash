#!/bin/bash

function log() {
  echo "${@}" >&2
}

function logf() {
  printf "${@}\n" >&2
}

# ──────────────────────────── elastic mock helpers ───────────────────────────

ELASTIC_MOCK_PID=""

# start_elastic_mock CAPTURE_FILE PORT
# Starts the elastic_mock server on 127.0.0.1:PORT and waits until it accepts
# connections.  The server PID is stored in ELASTIC_MOCK_PID.
# Use `elastic_mock free-port` to obtain a free port before calling this.
function start_elastic_mock() {
    local capture_file="$1"
    local port="$2"
    elastic_mock serve "$capture_file" "$port" &
    ELASTIC_MOCK_PID=$!
    elastic_mock wait-server "$port" --timeout 5
}

function stop_elastic_mock() {
    if [[ -n "${ELASTIC_MOCK_PID:-}" ]]; then
        kill "$ELASTIC_MOCK_PID" 2>/dev/null || true
        wait "$ELASTIC_MOCK_PID" 2>/dev/null || true
        ELASTIC_MOCK_PID=""
    fi
}

# wait_elastic_post CAPTURE_FILE [TIMEOUT_SECONDS [COUNT]]
# Returns 0 once at least COUNT POSTs have been captured, 1 on timeout.
function wait_elastic_post() {
    elastic_mock wait "$1" --timeout "${2:-10}" --count "${3:-1}"
}

function run_srun_unchecked() {
  log "+ srun $@"
  run srun -n1 --oversubscribe "$@"

  log "${output}"

  echo "+ exit status: ${status}"
}

function run_srun() {
  run_srun_unchecked "$@"
  [ "${status}" -eq 0 ]
}

function run_sbatch_unchecked() {
  slurm_log=$(mktemp)
  run sbatch --wait --output "${slurm_log}" "$@"
  log "${output}"
  logf "+ job log (${slurm_log}):\n$(cat ${slurm_log})"
}

function run_sbatch() {
  run_sbatch_unchecked "$@"
  [ "${status}" -eq 0 ]
}

# Clear and recreate the scratch directory $1.
#
# Some tests need root-owned fixtures, to check that an image the caller cannot
# read is refused. Those cannot be removed by the test user, so a run that was
# interrupted before its teardown leaves them behind - and a plain `rm -rf`
# then fails in setup(), failing whichever test happens to be first rather than
# reporting the real problem.
function reset_scratch_dir() {
    local dir="$1"

    if [ -d "$dir" ] && ! rm -rf "$dir" 2>/dev/null; then
        if sudo -n true 2>/dev/null; then
            sudo -n rm -rf "$dir"
        else
            # no way to clear it: let the original error surface
            rm -rf "$dir"
        fi
    fi
    mkdir -p "$dir"
}

#
# fixtures for the squashfs image access check
#

# Place a copy of the squashfs image $1 at $2, owned by root and mode 600, in a
# directory that anyone can traverse, and set $ROOT_OWNED_IMAGE to its path.
#
# The directory is deliberately traversable: stat() then succeeds, so the
# submit-side checks in the Slurm plugin's local context pass and the
# description reaches the root hook on the compute node. That is the path this
# fixture is for - an image the user can see but must not be able to read.
#
# Call this directly, never in a command substitution.
function make_root_owned_image() {
    local src="$1"
    local dst="$2"

    if [ "$(id -u)" = "0" ]; then
        skip "running as root: file permissions are bypassed"
    fi
    if ! sudo -n true 2>/dev/null; then
        skip "passwordless sudo is needed to create a root-owned image"
    fi

    sudo -n install -d -m 755 -o root -g root "$(dirname "$dst")"
    sudo -n install -m 600 -o root -g root "$src" "$dst"
    export ROOT_OWNED_IMAGE="$dst"
}

# Place a copy of the squashfs image $1 in directory $2, owned by root, mode
# 600, inside a root-owned mode-700 directory, and set $ROOT_ONLY_IMAGE to its
# path. Skips the test when that cannot be arranged, because creating a file
# the test user genuinely cannot read needs root.
#
# This is the fixture for the case that matters: an image the caller has no
# access to at all, which the privileged mount paths used to open anyway
# because they opened it as root.
#
# Call this directly, never in a command substitution - `skip` has no effect
# from a subshell.
function make_root_only_image() {
    local src="$1"
    local dir="$2"

    if [ "$(id -u)" = "0" ]; then
        skip "running as root: file permissions are bypassed"
    fi
    if ! sudo -n true 2>/dev/null; then
        skip "passwordless sudo is needed to create a root-owned image"
    fi

    sudo -n install -d -m 700 -o root -g root "$dir"
    sudo -n install -m 600 -o root -g root "$src" "$dir/image.squashfs"
    export ROOT_ONLY_IMAGE="$dir/image.squashfs"
}

# Place a copy of the squashfs image $1 at $2, readable only through one of the
# calling user's supplementary groups, and set $GROUP_IMAGE to its path and
# $GROUP_IMAGE_GID to the group used. Skips when the user has no supplementary
# group, or when the fixture cannot be created.
#
# The image has to be owned by *another* user: POSIX checks the owner class
# first and stops there, so a file the caller owns with mode 040 is unreadable
# by the caller no matter which group it carries. Hence root ownership, and
# hence sudo.
#
# Call this directly, never in a command substitution.
function make_group_readable_image() {
    local src="$1"
    local dst="$2"

    if [ "$(id -u)" = "0" ]; then
        skip "running as root: file permissions are bypassed"
    fi
    local gid
    gid=$(id -G | tr ' ' '\n' | grep -v "^$(id -g)$" | head -1)
    if [ -z "$gid" ]; then
        skip "the test user has no supplementary group"
    fi
    if ! sudo -n true 2>/dev/null; then
        skip "passwordless sudo is needed to create the image"
    fi

    sudo -n install -m 040 -o root -g "$gid" "$src" "$dst"
    export GROUP_IMAGE="$dst"
    export GROUP_IMAGE_GID="$gid"
}

# Remove a fixture created by make_root_only_image or
# make_group_readable_image.
function remove_root_only_image() {
    local dir="$1"
    sudo -n rm -rf "$dir" 2>/dev/null || true
}
