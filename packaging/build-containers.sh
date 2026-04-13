#!/usr/bin/env bash

# Build and push the container images used for RPM builds to GHCR.
# Run this script whenever the Dockerfiles change, on each target architecture.
#
# Before pushing, authenticate with GHCR:
#   echo $GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin

set -euo pipefail

scriptdir=$(realpath "$(dirname "${BASH_SOURCE[0]}")")

usage="$(basename "$0") [options]

Build (and optionally push) RPM build container images to GHCR.
Run on each target architecture (x86_64 and aarch64) when Dockerfiles change.

Options:
  -h, --help        show this help
  --os VERSION      build only this SLES version (default: all)
  --push            push image to GHCR after building
  --registry REG    registry prefix (default: ghcr.io/eth-cscs)
"

registry="ghcr.io/eth-cscs"
oslist="15.5 15.6"
do_push=false

TEMP=$(getopt -o h --long help,os:,push,registry: -- "$@")
if [ $? != 0 ]; then
    echo "${usage}" >&2
    exit 1
fi
eval set -- "$TEMP"

while true; do
    case "$1" in
    --os)       shift; oslist="$1"; shift ;;
    --push)     do_push=true; shift ;;
    --registry) shift; registry="$1"; shift ;;
    -h|--help)  echo "${usage}"; exit 0 ;;
    --)         shift; break ;;
    *)          echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

arch=$(uname -m)

for os in $oslist; do
    image="$registry/uenv-build-sles$os:$arch"
    echo "=== building $image"
    docker build -f "$scriptdir/dockerfiles/sles$os" -t "$image" "$scriptdir"
    if $do_push; then
        echo "=== pushing $image"
        docker push "$image"
    fi
done
