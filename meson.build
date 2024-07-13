project('uenv', ['cpp'],
        default_options : ['cpp_std=c++17'],
        version: files('VERSION'), meson_version: '>=0.57')
version = meson.project_version()

#default_mount_point = get_option('default_mount_point')

conf_data = configuration_data()

# organise the dependencies
sqlite3_dep = dependency('sqlite3')

cli11_dep = subproject('cli11', default_options: 'default_library=static').get_variable('CLI11_dep')
fmt_dep   = subproject('fmt', default_options: 'default_library=static').get_variable('fmt_dep')
catch_dep = subproject('catch2').get_variable('catch2_with_main_dep')

# the common implementation
lib_src = [
        'src/uenv/env.cpp',
        'src/uenv/lex.cpp',
        'src/uenv/parse.cpp',
]
lib_inc = include_directories('src')

lib_dep = declare_dependency(
        sources: lib_src,
        dependencies: [sqlite3_dep, fmt_dep],
        include_directories: lib_inc
)

# the uenv executable
uenv_src = [
        'src/cli/uenv.cpp',
        'src/cli/start.cpp',
]
uenv_dep = [sqlite3_dep]

uenv = executable('uenv',
        sources: uenv_src,
        dependencies: [uenv_dep, lib_dep, fmt_dep, cli11_dep],
        c_args: ['-DVERSION="@0@"'.format(version)],
        install: true)

unit_src = [
        'test/unit/env.cpp',
        'test/unit/parse.cpp',
        'test/unit/lex.cpp',
        'test/unit/main.cpp',
]

unit = executable('unit',
        sources: unit_src,
        dependencies: [catch_dep, lib_dep],
        build_by_default: true,
        install: false)

