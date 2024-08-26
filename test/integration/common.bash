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
  log "+ sbatch --wait $@"

  slurm_log=$(mktemp)
  run sbatch --wait -o "${slurm_log}" "$@"

  log "${output}"
  logf "+ job log (${slurm_log}):\n$(cat ${slurm_log})"
  rm -f "${slurm_log}"

  echo "+ exit status from sbatch: ${status}"
}

function run_sbatch() {
  run_sbatch_unchecked "$@"
  [ "${status}" -eq 0 ]
}
