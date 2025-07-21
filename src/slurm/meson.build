conf_data = configuration_data()
conf_data.set('default_mount_point', '/user-environment')
conf_data.set('uenv_repo_path_varname', '$SCRATCH/.uenv-images')
configure_file(input : 'config.hpp.in',
               output : 'config.hpp',
               configuration : conf_data)

module_src = ['plugin.cpp']

shared_module('slurm-uenv-mount',
              sources: module_src,
              dependencies: uenv_dep,
              cpp_args: ['-Wall', '-Wpedantic', '-Wextra'],
              install: true)

