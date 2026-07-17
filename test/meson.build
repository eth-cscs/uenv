sqlite3 = find_program('sqlite3', required: false)
if not sqlite3.found()
    error('sqlite3 must be installed to set up the test framework')
endif

integration_path = meson.current_source_dir() + '/integration'
setup_path = meson.current_source_dir() + '/setup'

bats_install_path = meson.current_build_dir() + '/external'
bats = custom_target(
    'bats-install',
    output: 'bats',
    command: [integration_path + '/install-bats', bats_install_path],
    build_by_default: true,
    install: false,
)

test_data_install_path = meson.current_build_dir() + '/data'
test_generated_data = custom_target (
    'test-generated-data',
    output: 'data',
    command: [setup_path + '/setup', test_data_install_path],
    build_by_default: true,
    install: false,
)

fs = import('fs')
fs.copyfile('integration/common.bash', 'common.bash')

# download zot for testing registries
zot_arch = (host_machine.cpu_family() == 'aarch64') ? 'arm64' : 'amd64'
zot_path = meson.current_build_dir() / 'zot'
custom_target('zot-binary',
    output: 'zot',
    command: [integration_path + '/install-zot', '@OUTPUT@', zot_arch],
    build_by_default: true,
    install: false,
)

if uenv_cli
  fs.copyfile('integration/cli.bats', 'cli.bats')
  # the registry suite drives uenv against a throwaway local zot registry.
  # registry_ctl (zot binary lifecycle) and listing_mock (uenv listing service
  # stand-in) are installed with execute permissions, like elastic_mock.
  custom_target('registry-ctl',
      input: 'integration/registry_ctl',
      output: 'registry_ctl',
      command: ['install', '-m', '755', '@INPUT@', '@OUTPUT@'],
      build_by_default: true,
      install: false,
  )
  custom_target('listing-mock',
      input: 'integration/listing_mock',
      output: 'listing_mock',
      command: ['install', '-m', '755', '@INPUT@', '@OUTPUT@'],
      build_by_default: true,
      install: false,
  )
  fs.copyfile('integration/registry.bats', 'registry.bats')
endif
if uenv_slurm_plugin
  custom_target('elastic-mock',
      input: 'integration/elastic_mock',
      output: 'elastic_mock',
      command: ['install', '-m', '755', '@INPUT@', '@OUTPUT@'],
      build_by_default: true,
      install: false,
  )
  fs.copyfile('integration/slurm.bats', 'slurm.bats')
endif
if uenv_squashfs_mount
  fs.copyfile('integration/squashfs-mount.bats', 'squashfs-mount.bats')
endif

suite_input = integration_path + '/setup_suite.bash.in'
suite_ouput = meson.current_build_dir() + '/setup_suite.bash'
setup_suite = custom_target (
    'setup-suite',
    input: suite_input,
    output: 'setup_suite.bash',
    command: [
        integration_path + '/install-setup_suite.sh',
        '@INPUT@',
        '@OUTPUT@',
        meson.global_source_root(),
        meson.global_build_root(),
    ],
    build_by_default: true,
    install: false,
)

unit_src = [
        'unit/curl.cpp',
        'unit/dates.cpp',
        'unit/env.cpp',
        'unit/envvars.cpp',
        'unit/fs.cpp',
        'unit/lex.cpp',
        'unit/main.cpp',
        'unit/mount.cpp',
        'unit/oci_auth.cpp',
        'unit/oci_client.cpp',
        'unit/oci_digest.cpp',
        'unit/oci_manifest.cpp',
        'unit/oci_parse.cpp',
        'unit/oci_registry.cpp',
        'unit/oci_tag.cpp',
        'unit/parse.cpp',
        'unit/sha.cpp',
        'unit/shell.cpp',
        'unit/signal.cpp',
        'unit/strings.cpp',
        'unit/repository.cpp',
        'unit/settings.cpp',
        'unit/subprocess.cpp',
        'unit/telemetry.cpp',
        'unit/url.cpp',
]

unit = executable('unit',
        sources: unit_src,
        dependencies: [catch_dep, uenv_dep],
        cpp_args: ['-DUENV_TEST_ZOT_PATH="' + zot_path + '"'],
        build_by_default: true,
        install: false)

test('unit', unit, is_parallel : false)
if uenv_cli
  test('cli', bats, args: ['./test/cli.bats'], is_parallel : false)
  test('registry', bats, args: ['./test/registry.bats'], is_parallel : false)
endif
if uenv_squashfs_mount
  test('squashfs-mount', bats, args: ['./test/squashfs-mount.bats'], is_parallel : false)
endif
