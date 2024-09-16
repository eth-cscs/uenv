version_path = meson.current_build_dir()+'config.h'
fs = import('fs')
if not fs.is_file(version_path)
  uenv_version = meson.project_version()
  uenv_version_array = uenv_version.split('-')
  if uenv_version_array.length()==2
    uenv_version_prerelease = uenv_version_array[1]
  else
    uenv_version_prerelease = ''
  endif
  uenv_version_array = uenv_version_array[0].split('.')
  uenv_version_major = uenv_version_array[0].to_int()
  uenv_version_minor = uenv_version_array[1].to_int()
  uenv_version_patch = uenv_version_array[2].to_int()

  conf = configuration_data()

  conf.set('version', uenv_version)
  conf.set('version_major', uenv_version_major)
  conf.set('version_minor', uenv_version_minor)
  conf.set('version_patch', uenv_version_patch)
  conf.set('version_prerelease', uenv_version_prerelease)

  configure_file(input : 'config.h.in',
                 output : 'config.h',
                 configuration : conf)
endif

