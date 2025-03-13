bash_completion = custom_target(
    'bash-completion',
    input: cli,
    output: cli.name(),
    command: ['env', 'PATH='+meson.current_build_dir()+'/..:$PATH', cli.name(), 'completion', 'bash'],
    install: true,
    capture: true,
    install_dir: get_option('datadir')+'/bash-completion/completions',
)
