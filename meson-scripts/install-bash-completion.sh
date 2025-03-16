#!/usr/bin/env bash

prefix="${DESTDIR}/${MESON_INSTALL_PREFIX}/share/bash-completion/completions"
mkdir -p "${prefix}"

${MESON_BUILD_ROOT}/uenv completion bash > "${prefix}/uenv"
