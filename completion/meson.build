fs = import('fs')

bash_completion = custom_target(
    'bash-completion',
    input: cli,
    output: 'uenv',
    command: [meson.current_source_dir() + '/bash.sh', meson.global_build_root(), meson.current_build_dir()],
    install: true,
    capture: true,
    install_dir: get_option('datadir')+'/bash-completion/completions',
)
