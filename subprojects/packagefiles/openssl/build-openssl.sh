#!/bin/sh
#
# Drive OpenSSL's own Configure + make from meson. See meson.build for why the
# work is split in two.
#
#   build-openssl.sh configure PERL MAKE SRCDIR BUILDDIR CONFIGURE_ARG...
#   build-openssl.sh build     MAKE BUILDDIR
#

set -eu

phase=$1
shift

case $phase in
configure)
    perl=$1
    make=$2
    src=$3
    bld=$4
    shift 4
    args=$*
    stamp=$bld/.uenv-configure-args

    mkdir -p "$bld"

    if [ -f "$bld/configdata.pm" ] && [ -f "$stamp" ] &&
       [ "$(cat "$stamp")" = "$args" ]; then
        # already configured with exactly these options. Regenerating the
        # headers is a no-op, but run it anyway so that an interrupted earlier
        # run cannot leave a half-generated include tree behind.
        cd "$bld"
        exec "$make" build_generated
    fi

    # the options changed: drop objects built with the old ones. $bld is
    # always a subdirectory of the meson build dir, never the build dir
    # itself.
    if [ -f "$bld/Makefile" ]; then
        (cd "$bld" && "$make" clean) >/dev/null 2>&1 || true
    fi
    rm -f "$stamp"

    # out of tree, so that the unpacked tarball stays pristine and
    # `meson setup --wipe` really does give a clean OpenSSL build.
    (cd "$bld" && "$perl" "$src/Configure" "$@")

    # writes the generated public headers (ssl.h, x509.h, opensslv.h, ...).
    # curl's configure-time header probes need these to exist before this
    # subproject returns.
    (cd "$bld" && "$make" build_generated)

    printf '%s' "$args" > "$stamp"
    ;;

build)
    make=$1
    bld=$2

    # meson has no way to hand a foreign build system its job count. nproc
    # honours the cpu affinity mask, so this is right inside a container and
    # under taskset; UENV_OPENSSL_JOBS overrides it.
    jobs=${UENV_OPENSSL_JOBS:-$(nproc 2>/dev/null || echo 4)}

    cd "$bld"
    # build_libs implies build_generated and `make depend`. On an up to date
    # tree this is a no-op, which is what makes it cheap to re-run on every
    # meson regeneration.
    "$make" -j "$jobs" build_libs
    test -f libcrypto.a
    test -f libssl.a
    ;;

*)
    echo "$0: unknown phase '$phase'" >&2
    exit 1
    ;;
esac
