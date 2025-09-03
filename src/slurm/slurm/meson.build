# Set up the external slurm dependency: slurm_dep
#
# To build a Slurm plugin we only require the following headers:
#     slurm/slurm_version.h
#     slurm/spank.h
#     slurm/slurm_errno.h
# By default the slurm_version is 0.0.0, which we interpret as "use the version
# of Slurm installed on the system", i.e. slurm_dep will be an empty dependency.
#
# Otherwise, download the tar ball for the selected version of slurm and extract
# only the required headers using the get-slurm.sh script.
# The header files will be installed in the current path, which is added to the
# include search path in the slurm_dep dependency.

slurm_version_parts = slurm_version.split('.')
if slurm_version_parts.length() != 3
  error('slurm_version must be in major.minor.patch format')
endif

slurm_major = slurm_version_parts[0].to_int()
slurm_minor = slurm_version_parts[1].to_int()
slurm_patch = slurm_version_parts[2].to_int()

# download the slurm source code if requested
if slurm_major > 0
  message('DOWNLOADING SLURM')
  slurm_path   = meson.current_build_dir()
  slurm_script = meson.project_source_root() +  '/meson-scripts/get-slurm.sh'
  slurm_headers = custom_target(
        'slurm-headers',
        output: ['version.h', 'spank.h', 'slurm_errno.h'],
        command: [slurm_script, slurm_major.to_string(),
                  slurm_minor.to_string(), slurm_patch.to_string(), slurm_path],
        install: false,
        install_dir: get_option('libexecdir'),
  )
  slurm_inc = include_directories('..')
  slurm_dep = declare_dependency(
    include_directories: slurm_inc,
    sources: slurm_headers,
  )
else
  slurm_inc = include_directories()
  slurm_dep = declare_dependency(
    include_directories: slurm_inc,
  )
endif

