conf_data = configuration_data()
configure_file(input : 'config.hpp.in',
               output : 'config.hpp',
               configuration : conf_data)

subdir('slurm')
module_src = ['plugin.cpp', 'mount_slurm.cpp', 'elastic.cpp']

shared_module('slurm-uenv-mount',
              sources: module_src,
              dependencies: [uenv_dep, slurm_dep],
              cpp_args: ['-Wall', '-Wpedantic', '-Wextra'],
              install: true)
