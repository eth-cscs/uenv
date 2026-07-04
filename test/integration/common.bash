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

# ──────────────────────── registry (zot) + listing helpers ───────────────────

REGISTRY_STATE=""
LISTING_MOCK_PID=""

# start_registry STATE PORT: run a throwaway zot registry on 127.0.0.1:PORT.
function start_registry() {
    REGISTRY_STATE="$1"
    local port="$2"
    registry_ctl serve "$REGISTRY_STATE" "$port" &
    registry_ctl wait "$port" --timeout 30
}

function stop_registry() {
    if [[ -n "${REGISTRY_STATE:-}" ]]; then
        registry_ctl kill "$REGISTRY_STATE" 2>/dev/null || true
        REGISTRY_STATE=""
    fi
}

# start_listing LISTING_FILE PORT: run the listing_mock server on 127.0.0.1:PORT.
function start_listing() {
    local listing_file="$1"
    local port="$2"
    listing_mock serve "$listing_file" "$port" &
    LISTING_MOCK_PID=$!
    listing_mock wait-server "$port" --timeout 5
}

function stop_listing() {
    if [[ -n "${LISTING_MOCK_PID:-}" ]]; then
        kill "$LISTING_MOCK_PID" 2>/dev/null || true
        wait "$LISTING_MOCK_PID" 2>/dev/null || true
        LISTING_MOCK_PID=""
    fi
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
