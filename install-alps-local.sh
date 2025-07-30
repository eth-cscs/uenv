#!/bin/bash

set -eu

# build static dependencies
(
    cd extern/
    if [ ! -d "zstd-1.5.7/install/lib/pkgconfig" ]; then
        (
            curl -L https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz | tar xzf -
            cd zstd-1.5.7/build/meson
            CXX=g++-12 CC=gcc-12 uvx --with=meson,ninja \
                       meson setup \
                       --buildtype=release \
                       -Dbin_programs=true \
                       -Dbin_contrib=true \
                       --prefix=$PWD/../../install \
                       --wipe builddir
            uvx --with=meson,ninja ninja -C builddir install
            cd ../../install
            # squashfuse expects a lib dir
            ln -sf lib64 lib
        )
    fi

    export ZSTD=$(realpath zstd-1.5.7/install)

    if [ ! -d "squashfuse-0.6.1/install/lib/pkgconfig" ]; then
        (
            curl -L https://github.com/vasi/squashfuse/releases/download/0.6.1/squashfuse-0.6.1.tar.gz | tar xzf -
            cd squashfuse-0.6.1

            # patch includedir in pkg-config
            sed -i 's/^includedir=.*/includedir=@includedir@/' squashfuse.pc.in
            sed -i 's/^includedir=.*/includedir=@includedir@/' squashfuse_ll.pc.in

            ./configure --disable-high-level \
                --enable-static=yes \
                --enable-shared=no \
                --without-lzo \
                --without-lz4 \
                --with-zstd=$ZSTD \
                --prefix=$PWD/install
            make install
        )
    fi
)

PKG_CONFIG_PATH=$(realpath extern/zstd-1.5.7/install/lib/pkgconfig):$(realpath extern/squashfuse-0.6.1/install/lib/pkgconfig):${PKG_CONFIG_PATH:-}
export PKG_CONFIG_PATH

arch=$(uname -m)
echo "== architecture: $arch"

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
build=$root/build-alps-$arch
pyenv=$root/pyenv-alps-$arch
install=$HOME/.local/$arch
uenv_version=$(cat VERSION)

rm -rf $build
rm -rf $pyenv
echo "== configure in $build"

export CC=gcc-12
export CXX=g++-12
uvx --with meson,ninja meson setup -Dsquashfs_mount=true -Dfuse=true --prefix=$install $build $root
uvx --with meson,ninja meson compile -C$build
uvx --with meson,ninja meson install -C$build --skip-subprojects

echo ""
echo "== succesfully installed"

echo "======================================================================================="
echo "add the following to your ~/.bashrc (or equivalent for .zshrc):"
echo ""
echo "Note:"
echo "- log out and in again for the changes to take effect"
echo "- if successful, 'uenv --version' will report version ${uenv_version}"
echo ""
echo "export PATH=$install/bin:\$PATH"
echo "unset -f uenv"
