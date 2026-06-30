conf_data = configuration_data()
configure_file(input : 'config.hpp.in',
               output : 'config.hpp',
               configuration : conf_data)

subdir('slurm')
module_src = [
  'elastic.cpp',
  'environ.cpp',
  'mount_slurm.cpp',
  'plugin.cpp',
]
cpp_args_list = ['-Wall', '-Wpedantic', '-Wextra']
slurm_fuse = get_option('slurm_fuse')

dependencies = [uenv_dep, slurm_dep]
if slurm_fuse
    module_src += ['../uenv/rootless.cpp', '../uenv/mutable_root.cpp', 'plugin_fuse.cpp']
    cpp_args_list += ['-DUENV_FUSE']
    dependencies += [squashfuse_dep, fuse_dep]
endif

shared_module('slurm-uenv-mount',
              sources: module_src,
              dependencies: dependencies,
              cpp_args: cpp_args_list,
              install: true)
