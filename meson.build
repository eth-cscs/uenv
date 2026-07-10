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
# zlib: needed for gzip handling in the native OCI registry client that
# replaced the oras binary. Always built from the vendored wrapdb subproject
# (never the system copy) and statically linked, so the binary is self-contained.
# Built *before* curl so its override_dependency('zlib') is in place and curl
# links the same vendored static zlib rather than the system copy.
zlib_dep = subproject('zlib', default_options: ['warning_level=0', 'werror=false', 'default_library=static']).get_variable('zlib_dep')
subproject('curl', default_options: ['werror=false', 'warning_level=0', 'tests=disabled', 'unittests=disabled', 'tool=disabled'])
curl_dep = dependency('libcurl', required: true)
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
        'src/oci/util.cpp',
        'src/uenv/parse.cpp',
        'src/uenv/print.cpp',
        'src/uenv/repository.cpp',
        'src/uenv/settings.cpp',
        'src/uenv/telemetry.cpp',
        'src/uenv/uenv.cpp',
        'src/util/color.cpp',
        'src/util/curl.cpp',
        'src/util/envvars.cpp',
        'src/util/fs.cpp',
        'src/util/lex.cpp',
        'src/util/lustre.cpp',
        'src/util/parse.cpp',
        'src/util/semver.cpp',
        'src/util/sha256.cpp',
        'src/util/shell.cpp',
        'src/util/signal.cpp',
        'src/util/strings.cpp',
        'src/util/subprocess.cpp',
        'src/util/toml.cpp',
]
lib_inc = include_directories('src')

lib_uenv = static_library(
        'uenv',
        lib_src,
        include_directories: lib_inc,
        dependencies: [curl_dep, zlib_dep, sqlite3_dep, fmt_dep, spdlog_dep, json_dep, barkeep_dep, toml_dep],
)

uenv_dep = declare_dependency(
        link_with: lib_uenv,
        dependencies: [curl_dep, zlib_dep, sqlite3_dep, fmt_dep, spdlog_dep, json_dep, barkeep_dep, libmount_dep, toml_dep],
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
