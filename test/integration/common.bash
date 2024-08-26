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
  # workaround for `run sbatch --wait` always returning status==0
  # grep slurm log for 'srun: error'
  run bash -c "cat $slurm_log | grep -q 'srun: error'"
  [ ! "${status}" -eq 0 ]
}
