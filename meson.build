project('uenv', ['cpp'],
        default_options : ['cpp_std=c++17'],
        version: files('VERSION'), meson_version: '>=0.57')
version = meson.project_version()

#default_mount_point = get_option('default_mount_point')

conf_data = configuration_data()

sqlite3_dep = dependency('sqlite3')

#lib_src = ['src/lib/.cpp',
#           'src/lib/strings.cpp']

uenv_src = ['src/uenv.cpp'] + lib_src
uenv_inc = include_directories('include')
uenv_dep = [sqlite3_dep]

exec = executable('uenv',
              sources: uenv_src,
              dependencies: uenv_dep,
              c_args: ['-DVERSION="@0@"'.format(version)],
              install: true)

#unit_inc = include_directories('external/catch2')

#unit_src = ['external/catch2/catch_amalgamated.cpp',
#'tests/unit/parse_args.cpp',
#'tests/unit/strings.cpp',]
#unit_src += lib_src

#unit = executable('unit',
#sources: unit_src,
#include_directories: [unit_inc, module_inc],
#dependencies: module_dep,
#build_by_default: true,
#install: false)


