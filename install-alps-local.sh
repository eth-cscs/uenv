#!/bin/bash

root=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)
build=$root/build-alps
pyenv=$root/pyenv-alps
python3 -m venv $pyenv
source $pyenv/bin/activate
pip install --upgrade pip
pip install meson ninja
CC=gcc-12 CXX=g++-12 meson setup --prefix=$HOME/.local $build $root
meson compile -C$build
meson install -C$build --skip-subprojects
