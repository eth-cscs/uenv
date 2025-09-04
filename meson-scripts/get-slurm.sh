#!/usr/bin/env bash

major="$1"
minor="$2"
patch="$3"
destination="$4"

# create version in yy.mm.p form (i.e. with the month padded with 0 to always be 2 digits)
slurm_version=$(printf '%d.%02d.%d' $major $minor $patch)
# The slurm version is hardcoded in the header in hexadecimal format, [major][minor][patch] (two digits each)
slurm_hex_version=$(printf '0x%02x%02x%02x' $major $minor $patch)

url=https://download.schedmd.com/slurm/slurm-${slurm_version}.tar.bz2

slurm_version_path="slurm-${slurm_version}"
slurm_download_path="$(pwd)/${slurm_version_path}/slurm"
curl --silent --location $url | tar -xj "${slurm_version_path}/slurm/spank.h" \
                       "${slurm_version_path}/slurm/slurm_errno.h" \
                       "${slurm_version_path}/slurm/slurm_version.h.in"

# Create slurm_version.h from .in
sed "s/^#undef SLURM_VERSION_NUMBER.*/#define SLURM_VERSION_NUMBER $slurm_hex_version/" \
    "${slurm_download_path}/slurm_version.h.in" > "$destination/slurm_version.h"
cp  "${slurm_download_path}/spank.h"              "${destination}/spank.h"
cp  "${slurm_download_path}/slurm_errno.h"        "${destination}/slurm_errno.h"
