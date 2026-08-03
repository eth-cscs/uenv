project('uenv', ['cpp'],
        default_options : [
          'cpp_std=c++20',
          'default_library=static',
          'werror=true',
          'warning_level=3',
        ],
        version: files('VERSION'),
        meson_version: '>=1.4')
version = meson.project_version()

uenv_slurm_plugin = get_option('slurm_plugin')
uenv_cli = get_option('cli')
uenv_squashfs_mount = get_option('squashfs_mount')

conf_data = configuration_data()

add_global_arguments('-Wno-missing-field-initializers', language : 'cpp')

# fixes warnings generated when building curl and sqlite3
# that started to occur with gcc 15.2.1
# Note that we only apply this to C, so it should not be applied to the C++ code
add_global_arguments('-Wno-discarded-qualifiers', language : 'c')

# configure the dependencies

# use meson wrap to provide the dependencies, because the 'default_library=static' option
# will be propogated to each dependency, for a statically linked executable.
cli11_dep = subproject('cli11', default_options: ['werror=false', 'warning_level=0']).get_variable('CLI11_dep')
fmt_dep   = subproject('fmt', default_options: ['werror=false', 'warning_level=0']).get_variable('fmt_dep')
json_dep  = subproject('nlohmann_json', default_options: ['werror=false', 'warning_level=0']).get_variable('nlohmann_json_dep')
spdlog_dep  = subproject('spdlog', default_options: ['werror=false', 'warning_level=0','std_format=disabled','external_fmt=enabled']).get_variable('spdlog_dep')
sqlite3_dep = subproject('sqlite3', default_options: ['werror=false', 'warning_level=0']).get_variable('sqlite3_dep')
toml_base = subproject('tomlplusplus', default_options: ['werror=false', 'warning_level=0', 'build_lib=false']).get_variable('tomlplusplus_dep')
toml_dep = declare_dependency(
  dependencies: toml_base,
  compile_args: ['-DTOML_EXCEPTIONS=0', '-DTOML_HEADER_ONLY=0'],
)
# zlib: needed for gzip handling in the native OCI registry client
# Built before curl so override_dependency('zlib') is in place for when curl is configured
zlib_dep = subproject('zlib', default_options: ['warning_level=0', 'werror=false', 'default_library=static']).get_variable('zlib_dep')
# openssl: the TLS backend for curl.
# Built before curl so override_dependency('openssl') is in place for when curl is configured
openssl_dep = subproject('openssl', default_options: ['warning_level=0', 'werror=false', 'default_library=static']).get_variable('openssl_dep')
# Every curl feature is pinned explicitly. Almost all of them default to 'auto',
# which means they switch on wherever the build host happens to have the dev
# package installed - the binary we ship would then have runtime dependencies
# that vary with the machine it was built on. Nothing here is left to chance.
subproject('curl',
        default_options: [
            'werror=false', 'warning_level=0',
            'tests=disabled', 'unittests=disabled', 'tool=disabled',
            'libuv=disabled', # only used by curl's own tests, and only in debug builds
            # TLS, via the vendored static openssl configured above
            'ssl=enabled', 'openssl=enabled',
            # the three features uenv actually relies on:
            #   ipv6      - registries that only publish a AAAA record
            #   asynchdns - threaded resolver, so the connect/total timeouts can
            #               interrupt a hung DNS lookup
            #   libz      - Content-Encoding: gzip, via the vendored static zlib
            'ipv6=enabled', 'asynchdns=enabled', 'libz=enabled',
            # everything else off. uenv speaks HTTP/1.1 over http(s) and nothing
            # else - see CURLOPT_PROTOCOLS_STR in src/util/curl.cpp - and
            # authenticates with Basic plus its own Authorization: Bearer header,
            # so none of the protocol, auth or name-resolution extras below are
            # reachable at runtime. They can be turned back on individually if a
            # feature ever needs one.
            'http2=disabled', 'brotli=disabled', 'zstd=disabled',
            'psl=disabled', 'idn=disabled', 'gsasl=disabled',
            'gss-api=disabled', 'ntlm=disabled', 'tls-srp=disabled',
            'alt-svc=disabled', 'hsts=disabled', 'unixsockets=disabled',
            'ssh=disabled', 'ldap=disabled', 'ldaps=disabled', 'rtmp=disabled',
            'dict=disabled', 'ftp=disabled', 'gopher=disabled', 'imap=disabled',
            'mqtt=disabled', 'pop3=disabled', 'rtsp=disabled', 'smb=disabled',
            'smtp=disabled', 'telnet=disabled', 'tftp=disabled',
        ])
curl_dep = dependency('libcurl', required: true)
# libarchive: used by the native OCI registry client to pack/unpack directories
libarchive_dep = subproject('libarchive',
        default_options: [
            'warning_level=0', 'werror=false', 'default_library=static',
            'progs=[]', 'tests=disabled', 'regex=disabled',
            'zlib=enabled',
            'bz2lib=disabled', 'libb2=disabled', 'lz4=disabled', 'zstd=disabled',
            'lzma=disabled', 'lzo2=disabled', 'iconv=disabled', 'cng=disabled',
            'openssl=disabled', 'xml2=disabled', 'expat=disabled',
            'xattr=disabled', 'acl=disabled',
        ]).get_variable('libarchive_dep')
barkeep_dep = declare_dependency(
        include_directories: 'extern/barkeep',
        compile_args: ['-DBARKEEP_ENABLE_FMT_FORMAT'],
)

libmount_dep = dependency('mount')

# the lib dependency is all of the common funtionality shared between the CLI
# and the slurm plugin.
lib_src = [
        'src/site/site.cpp',
        'src/uenv/elastic.cpp',
        'src/uenv/env.cpp',
        'src/uenv/log.cpp',
        'src/uenv/meta.cpp',
        'src/uenv/mount.cpp',
        'src/oci/auth.cpp',
        'src/oci/client.cpp',
        'src/oci/digest.cpp',
        'src/oci/manifest.cpp',
        'src/oci/parse.cpp',
        'src/oci/pull.cpp',
        'src/oci/push.cpp',
        'src/oci/reference.cpp',
        'src/oci/tag.cpp',
        'src/oci/types.cpp',
        'src/oci/util.cpp',
        'src/uenv/parse.cpp',
        'src/uenv/print.cpp',
        'src/uenv/repository.cpp',
        'src/uenv/settings.cpp',
        'src/uenv/telemetry.cpp',
        'src/uenv/uenv.cpp',
        'src/util/archive.cpp',
        'src/util/color.cpp',
        'src/util/curl.cpp',
        'src/util/envvars.cpp',
        'src/util/fs.cpp',
        'src/util/lex.cpp',
        'src/util/lustre.cpp',
        'src/util/parse.cpp',
        'src/util/semver.cpp',
        'src/util/sha.cpp',
        'src/util/shell.cpp',
        'src/util/signal.cpp',
        'src/util/strings.cpp',
        'src/util/subprocess.cpp',
        'src/util/toml.cpp',
        'src/util/url.cpp',
]
lib_inc = include_directories('src')

lib_uenv = static_library(
        'uenv',
        lib_src,
        include_directories: lib_inc,
        dependencies: [curl_dep, zlib_dep, libarchive_dep, sqlite3_dep, fmt_dep, spdlog_dep, json_dep, barkeep_dep, toml_dep],
)

uenv_dep = declare_dependency(
        link_with: lib_uenv,
        dependencies: [curl_dep, zlib_dep, libarchive_dep, sqlite3_dep, fmt_dep, spdlog_dep, json_dep, barkeep_dep, libmount_dep, toml_dep],
        include_directories: lib_inc
)

subdir('src/uenv')

# the uenv executable
if uenv_cli
    cli_src = [
            'src/cli/add_remove.cpp',
            'src/cli/config.cpp',
            'src/cli/copy.cpp',
            'src/cli/delete.cpp',
            'src/cli/find.cpp',
            'src/cli/help.cpp',
            'src/cli/image.cpp',
            'src/cli/inspect.cpp',
            'src/cli/ls.cpp',
            'src/cli/pull.cpp',
            'src/cli/push.cpp',
            'src/cli/repo.cpp',
            'src/cli/run.cpp',
            'src/cli/start.cpp',
            'src/cli/status.cpp',
            'src/cli/uenv.cpp',
            'src/cli/util.cpp',
            'src/cli/build.cpp',
            'src/cli/completion.cpp',
    ]

    xxd = find_program('xxd', required : true)
    completion_bash_dep = custom_target(
        'completion-bash-dep',
        input: 'src/cli/completion.bash',
        output: 'completion.bash.h',
        command: [xxd, '-i', '-n', 'completion_bash_main', '@INPUT@', '@OUTPUT@'])

    cli = executable('uenv',
            sources: cli_src + completion_bash_dep,
            dependencies: [uenv_dep, sqlite3_dep, fmt_dep, spdlog_dep, cli11_dep, barkeep_dep],
            c_args: [
              '-DVERSION="@0@"'.format(version),
            ],
            install: true)

    meson.add_install_script('./meson-scripts/install-bash-completion.sh')
endif

if uenv_slurm_plugin
    slurm_version = get_option('slurm_version')
    subdir('src/slurm')
endif

if uenv_squashfs_mount
    squashfs_mount = executable('squashfs-mount',
          sources: ['src/squashfs-mount/squashfs-mount.cpp'],
          dependencies: [uenv_dep, fmt_dep, cli11_dep],
          c_args: [
            '-DVERSION="@0@"'.format(version),
          ],
          install_mode: ['rwsr-xr-x', 'root', 'root'],
          install: true)
endif

if get_option('tests').enabled()
    catch_dep = subproject('catch2', default_options: ['werror=false', 'warning_level=0', 'tests=false']).get_variable('catch2_with_main_dep')
    subdir('test')
endif
