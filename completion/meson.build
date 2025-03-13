fs = import('fs')

bash_completion = custom_target(
    'bash-completion',
    input: cli,
    output: cli.name(),
    command: ['env', 'PATH='+fs.parent(cli.full_path())+':$PATH', cli.name(), 'completion', 'bash'],
    install: true,
    capture: true,
    install_dir: get_option('datadir')+'/bash-completion/completions',
)
