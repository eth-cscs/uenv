conf_data = configuration_data()
conf_data.set('default_mount_point', '/user-environment')
conf_data.set('uenv_repo_path_varname', '$SCRATCH/.uenv-images')
configure_file(input : 'config.hpp.in',
               output : 'config.hpp',
               configuration : conf_data)

libmount_dep = dependency('mount')

module_src = ['plugin.cpp', 'mount.cpp']

module_dep = [libmount_dep, sqlite3_dep, uenv_dep]

shared_module('slurm-uenv-mount',
              sources: module_src,
              dependencies: module_dep,
              cpp_args: ['-Wall', '-Wpedantic', '-Wextra'],
              install: true)

