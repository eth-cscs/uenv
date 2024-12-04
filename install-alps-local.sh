#!/bin/bash

set -e

root=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)
build=$root/build-alps
pyenv=$root/pyenv-alps
log=$root/log_local

echo "== use python to install up meson and ninja"
python3 -m venv $pyenv > $log
source $pyenv/bin/activate >> $log
pip install --upgrade pip >> $log
pip install meson ninja >> $log
echo "== configure"
CC=gcc-12 CXX=g++-12 meson setup --prefix=$HOME/.local $build $root >> $log
echo "== build"
meson compile -C$build >> $log
echo "== install"
meson install -C$build --skip-subprojects >> $log

echo ""
echo "== succesfully installed"

echo "======================================================================================="
echo "add the following to your ~/.bashrc (or equivalent for .zshrc):"
echo ""
echo "Note:"
echo "- log out and in again for the changes to take effect"
echo "- if successful, 'uenv --version' will report version 6"
echo ""
echo "export PATH=$HOME/.local/bin"
echo "unset -f uenv"
