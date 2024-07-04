project('uenv', ['cpp'],
        default_options : ['cpp_std=c++17'],
        version: files('VERSION'), meson_version: '>=0.57')
version = meson.project_version()

#default_mount_point = get_option('default_mount_point')

conf_data = configuration_data()

sqlite3_dep = dependency('sqlite3')

cli11 = subproject('cli11', default_options: 'default_library=static')
cli11_dep = cli11.get_variable('CLI11_dep')

fmt = subproject('fmt', default_options: 'default_library=static')
fmt_dep = fmt.get_variable('fmt_dep')

uenv_src = ['src/uenv.cpp', 'src/start.cpp']
uenv_inc = include_directories('include')
uenv_dep = [sqlite3_dep]

uenv = executable('uenv',
              sources: uenv_src,
              dependencies: [uenv_dep, fmt_dep, cli11_dep],
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


