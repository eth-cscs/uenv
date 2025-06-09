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
fs.copyfile('integration/cli.bats', 'cli.bats')
fs.copyfile('integration/slurm.bats', 'slurm.bats')
fs.copyfile('integration/common.bash', 'common.bash')

suite_input = integration_path + '/setup_suite.bash.in'
suite_ouput = meson.current_build_dir() + '/setup_suite.bash'
setup_suite = custom_target (
    'setup-suite',
    output: 'setup_suite.bash',
    command: [
        integration_path + '/install-setup_suite.sh',
        suite_input,
        suite_ouput,
        meson.source_root(),
        meson.build_root(),
    ],
    build_by_default: true,
    install: false,
)

unit_src = [
        'unit/dates.cpp',
        'unit/env.cpp',
        'unit/envvars.cpp',
        'unit/fs.cpp',
        'unit/lex.cpp',
        'unit/main.cpp',
        'unit/parse.cpp',
        'unit/signal.cpp',
        'unit/strings.cpp',
        'unit/repository.cpp',
        'unit/settings.cpp',
        'unit/subprocess.cpp',
]

unit = executable('unit',
        sources: unit_src,
        dependencies: [catch_dep, uenv_dep],
        build_by_default: true,
        install: false)

test('unit', unit, is_parallel : false)
test('cli', bats, args: ['./test/cli.bats'], is_parallel : false)
