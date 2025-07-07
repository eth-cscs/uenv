#!/bin/bash

function log() {
  echo "${@}" >&2
}

function logf() {
  printf "${@}\n" >&2
}

function run_srun_unchecked() {
  log "+ srun $@"
  run srun -N1 --oversubscribe "$@"

  log "${output}"

  echo "+ exit status: ${status}"
}

function run_srun() {
  run_srun_unchecked "$@"
  [ "${status}" -eq 0 ]
}

function run_sbatch_unchecked() {
  slurm_log=$(mktemp)
  run sbatch --wait -o "${slurm_log}" "$@"
  log "${output}"
  logf "+ job log (${slurm_log}):\n$(cat ${slurm_log})"
}

function run_sbatch() {
  run_sbatch_unchecked "$@"
  [ "${status}" -eq 0 ]
}

# Docker-based test registry setup and teardown functions
function setup_test_registry() {
    # Check if Docker is available
    if ! command -v docker &> /dev/null; then
        skip "Docker not available for registry tests"
    fi

    # Use a random high port to avoid conflicts
    export REGISTRY_PORT=$((5000 + RANDOM % 1000))
    export REGISTRY_URL="localhost:$REGISTRY_PORT"
    export REGISTRY_CONTAINER="uenv-test-registry-$$"

    log "Setting up test registry on port $REGISTRY_PORT"

    # Start Zot registry container
    docker run -d \
        --name "$REGISTRY_CONTAINER" \
        -p "$REGISTRY_PORT:5000" \
        ghcr.io/project-zot/zot:latest || {
            skip "Failed to start Docker registry container"
        }

    # Wait for registry to be ready (up to 30 seconds)
    local count=0
    while [ $count -lt 30 ]; do
        if curl -s "http://localhost:$REGISTRY_PORT/v2/" > /dev/null 2>&1; then
            log "Test registry is ready at $REGISTRY_URL"
            return 0
        fi
        sleep 1
        count=$((count + 1))
    done

    # If we get here, the registry didn't start properly
    docker logs "$REGISTRY_CONTAINER" || true
    teardown_test_registry
    skip "Test registry failed to become ready"
}

function teardown_test_registry() {
    if [ -n "${REGISTRY_CONTAINER:-}" ]; then
        log "Cleaning up test registry container: $REGISTRY_CONTAINER"
        docker stop "$REGISTRY_CONTAINER" >/dev/null 2>&1 || true
        docker rm "$REGISTRY_CONTAINER" >/dev/null 2>&1 || true
        unset REGISTRY_CONTAINER
        unset REGISTRY_PORT
        unset REGISTRY_URL
    fi
}

function registry_is_available() {
    command -v docker &> /dev/null
}
