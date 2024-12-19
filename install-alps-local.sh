#!/bin/bash

set -e

arch=$(uname -m)
echo "== architecture: $arch"

root=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)
build=$root/build-alps-$arch
pyenv=$root/pyenv-alps-$arch
install=$HOME/.local/$arch

rm -rf $build
rm -rf $pyenv
echo "== configure in $build"
alias e="uenv run prgenv-gnu/24.11:v1 --view=defulat --"
CC=gcc-12 CXX=g++-12 e meson setup --prefix=$install $build $root
echo "== build"
e meson compile -C$build
echo "== install"
e meson install -C$build --skip-subprojects

echo ""
echo "== succesfully installed"

echo "======================================================================================="
echo "add the following to your ~/.bashrc (or equivalent for .zshrc):"
echo ""
echo "Note:"
echo "- log out and in again for the changes to take effect"
echo "- if successful, 'uenv --version' will report version 6"
echo ""
echo "export PATH=$install/bin:\$PATH"
echo "unset -f uenv"
