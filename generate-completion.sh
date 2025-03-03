#!/bin/bash

echo "Creating bash completion script in ${MESON_INSTALL_PREFIX}"
PATH=${MESON_INSTALL_PREFIX}/bin:$PATH uenv completion bash > ${MESON_INSTALL_PREFIX}/uenv-bash-completion.sh
