conf_data = configuration_data()
conf_data.set('default_mount_point', '/user-environment')
conf_data.set('uenv_repo_path_varname', '$SCRATCH/.uenv-images')
configure_file(input : 'config.hpp.in',
               output : 'config.hpp',
               configuration : conf_data)

libmount_dep = dependency('mount')

#lib_src = ['./lib/parse_args.cpp',
        #'./lib/database.cpp',
        #'./lib/filesystem.cpp',
        #'./lib/mount.cpp',
        #'./lib/sqlite.cpp',
        #'./lib/strings.cpp']

module_src = ['plugin.cpp', 'mount.cpp']

#module_inc = include_directories('src')

module_dep = [libmount_dep, sqlite3_dep, lib_dep]

shared_module('slurm-uenv-mount',
              sources: module_src,
              dependencies: module_dep,
              #include_directories: module_inc,
              cpp_args: ['-Wall', '-Wpedantic', '-Wextra'],
              install: true)

