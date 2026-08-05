# OpenSSL, built by OpenSSL's own Configure + make.
#
# Upstream ships no meson build, and wrapdb's port is frozen at 3.0.8 (see the
# comment in ../../openssl.wrap), so this wrap owns the build. It tells meson
# about exactly two things: the two static archives, and the two include trees
# - the one in the tarball, and the one Configure generates.
#
# Bumping the version is a URL + hash change in ../../openssl.wrap plus the
# `version:` below.

project(
    'openssl',
    'c',
    version: '3.5.7',
    license: 'Apache-2.0',
    meson_version: '>=1.1',
)

if host_machine.system() != 'linux'
    error('this OpenSSL wrap only supports linux hosts')
endif

cc = meson.get_compiler('c')

# found here rather than left to the script, so that a missing tool fails
# during `meson setup` with meson's own message instead of part way into a
# make log.
perl = find_program('perl', required: true)
make = find_program('make', 'gmake', required: true)
builder = files('build-openssl.sh')

# `perl Configure LIST` in the source tree prints every supported target.
openssl_targets = {
    'x86_64': 'linux-x86_64',
    'aarch64': 'linux-aarch64',
}
cpu = host_machine.cpu_family()
if not openssl_targets.has_key(cpu)
    error('no OpenSSL Configure target is known for cpu_family "@0@"'.format(cpu))
endif

configure_args = [
    openssl_targets[cpu],

    #
    # install layout
    #
    # `make install` is never run: these only set the paths that get compiled
    # into libcrypto. --openssldir is the one that matters, because it is
    # where SSL_CTX_set_default_verify_paths() looks for the trust store. The
    # wrapdb port left it pointing at a build prefix that exists on no host,
    # which is part of why src/util/curl.cpp discovers the CA bundle itself.
    '--prefix=/usr',
    '--openssldir=/etc/ssl',
    '--libdir=lib',

    #
    # what to build
    #
    # static only: this is what keeps `ldd build/uenv` down to libc, libstdc++,
    # libm and libgcc_s.
    'no-shared',
    # nothing is ever dlopen'ed: no providers or engines loaded from disk, and
    # so no -ldl on the link line either.
    'no-module',
    'no-dso',
    'no-engine',
    'no-afalgeng',
    # MD2/MD4/RC4/DES and friends. Unreachable from curl's TLS code.
    'no-legacy',
    # `make build_libs` builds none of these; disabling them keeps Configure's
    # generation step small.
    'no-apps',
    'no-docs',
    'no-tests',
    # TLS-level compression is a security liability, and enabling any of these
    # would pull a compression library off the build host into the binary -
    # the same reasoning that pins every curl feature in the top level
    # meson.build.
    'no-comp',
    'no-zlib',
    'no-brotli',
    'no-zstd',
    # curl uses the threaded resolver, so libcrypto has to be thread safe.
    # This is the default for linux-* targets; stated rather than assumed.
    'threads',

    #
    # compiler flags
    #
    # NOTE: passing any bare -flag switches Configure into "user added flags"
    # mode, in which it stops honouring CC/CFLAGS/CPPFLAGS/LDFLAGS from the
    # environment. That is deliberate - it makes the OpenSSL build independent
    # of whatever the RPM macros or a developer's shell export - but it does
    # mean CC has to be passed explicitly, below.
    #
    # libuenv.a ends up inside src/slurm/libslurm-uenv-mount.so, so every
    # object has to be position independent or that link fails.
    '-fPIC',
    # use the compiler meson picked, so that CI's CC="ccache gcc-12" still
    # goes through ccache and OpenSSL is not built by a different compiler
    # than the rest of the project.
    'CC=' + ' '.join(cc.cmd_array()),
]

openssl_src = meson.current_source_dir()
# built in a subdirectory of the subproject build dir so that OpenSSL's build
# tree and meson's never share a directory.
openssl_bld = meson.current_build_dir() / 'ossl'

#
# Configure and build, both at meson configure time
#
# Neither half can be deferred to build time:
#
#   - The headers, because most of OpenSSL 3.x's public headers - ssl.h,
#     x509.h, opensslv.h, configuration.h and ~20 more - do not exist in the
#     tarball; they are generated from .h.in templates. The top level
#     meson.build configures this subproject before curl's, and curl's
#     meson.build then probes us with cc.has_header_symbol('openssl/ssl.h',
#     ...) while it is being configured. Without the headers every one of
#     those probes would silently answer "no", and curl's own sources would
#     not compile.
#
#   - The archives, because a custom_target output cannot be handed to curl:
#     libcurl is `install: true` and would have to link_whole a non-installed
#     target, which meson rejects outright ("Cannot link_whole a custom or
#     Rust target ... into a static library 'curl'"). Building here instead
#     means find_library() below returns an ordinary external library, which
#     every meson version consumes without complaint.
#
# The cost is a quiet minute inside `meson setup` on a cold build directory
# (~8 s on 24 cores, ~40 s on a 4 core CI runner). Re-running after a meson
# regeneration is an up-to-date `make`, about a second.
#
run_command(
    builder, 'configure', perl.full_path(), make.full_path(),
    openssl_src, openssl_bld, configure_args,
    check: true,
)
run_command(builder, 'build', make.full_path(), openssl_bld, check: true)

# a relative include_directories() covers both the source and the build tree,
# which is exactly what OpenSSL needs: the static headers ship in the tarball
# (<src>/include), the generated ones are written by the run_command above
# (<build>/ossl/include).
openssl_inc = include_directories('include', 'ossl/include')

libcrypto_dep = declare_dependency(
    version: meson.project_version(),
    include_directories: openssl_inc,
    dependencies: [
        dependency('threads'),
        cc.find_library('crypto', dirs: [openssl_bld], static: true, required: true),
    ],
)

libssl_dep = declare_dependency(
    version: meson.project_version(),
    include_directories: openssl_inc,
    dependencies: [
        cc.find_library('ssl', dirs: [openssl_bld], static: true, required: true),
        libcrypto_dep,
    ],
)

# libssl depends on libcrypto rather than being listed beside it: static
# archive order matters to the linker, and an explicit dependency edge is what
# makes meson emit them in the right order.
openssl_dep = declare_dependency(
    version: meson.project_version(),
    dependencies: [libssl_dep, libcrypto_dep],
)

summary(
    {
        'version': meson.project_version(),
        'Configure target': openssl_targets[cpu],
        'openssldir': '/etc/ssl',
    },
    section: 'OpenSSL',
)
