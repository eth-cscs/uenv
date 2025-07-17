// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <util/expected.h>
#include <util/shell.h>

#include "help.h"
#include "run.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

std::string run_footer();

void run_args::add_cli(CLI::App& cli, global_settings& settings) {
    auto* run_cli = cli.add_subcommand("run", "run a uenv session");
    run_cli->add_option("-v,--view", view_description,
                        "comma separated list of views to load");
    run_cli
        ->add_option("uenv", uenv_description,
                     "comma separated list of uenv to mount")
        ->required();
    run_cli
        ->add_option("commands", commands,
                     "the command to run, including with arguments")
        ->required();
    run_cli->callback([&settings]() { settings.mode = uenv::cli_mode::run; });
    run_cli->footer(run_footer);
}

int run(const run_args& args, const global_settings& globals) {
    spdlog::info("run with options {}", args);

    // error if a uenv is already mounted
    if (in_uenv_session(globals.calling_environment)) {
        term::error("{}",
                    R"(a uenv session is already running.
It is not possible to call 'uenv start' or 'uenv run' inside a uenv session.
You need to finish the current session by typing 'exit' or hitting '<ctrl-d>'.)");
        return 1;
    }

    const auto env =
        concretise_env(args.uenv_description, args.view_description,
                       globals.config.repo, globals.calling_environment);

    if (!env) {
        term::error("{}", env.error());
        return 1;
    }

    //parse configuration.toml
    toml::table config;
    char *config_location = getenv("UENV_CONFIGURATION_PATH"); //gets config file from UENV_CONFIGURATION_PATH env variable
    std::string_view config_sv{config_location};
    try{
        config = toml::parse_file(config_sv);
    } catch(const toml::parse_error& err){
        term::error("{}", fmt::format("Error parsing configuration.toml:\n{}",err));
    }
    bool use_squashfuse = config["use_squashfuse"].value_or(false); //parse use_squashfuse key from configuration.toml

    auto runtime_environment =
        generate_environment(*env, globals.calling_environment, "SQFSMNT_FWD_");

    // generate the mount list
    std::vector<std::string> commands = {};
    
    if (use_squashfuse){
        commands.push_back("squashfs-mount-rootless");
    }
    else{
        commands.push_back("squashfs-mount");
    }

    for (auto e : env->uenvs) {
        commands.push_back(fmt::format("{}:{}", e.second.sqfs_path.string(),
                                       e.second.mount_path));
    }

    commands.push_back("--");
    commands.insert(commands.end(), args.commands.begin(), args.commands.end());

    // return util::exec(commands);
    return util::exec(commands, runtime_environment.c_env());
}

std::string run_footer() {
    using enum help::block::admonition;
    using help::block;
    using help::linebreak;
    using help::lst;
    std::vector<help::item> items{
        // clang-format off
        block{none, "Run a command in an environment."},
        linebreak{},
        block{xmpl, "run the script job.sh in an evironmnent"},
        block{code,   "uenv run prgenv-gnu/24.2:v1 -- ./job.sh"},
        block{none, "This will mount prgenv-gnu, execute job.sh, then return to the calling shell."},
        linebreak{},
        block{note, "how the command to execute comes after the two dashes '--'."},
        linebreak{},
        block{xmpl, "run the script job.sh in an evironmnent with a view loaded"},
        block{code,   "uenv run prgenv-gnu/24.2:v1 --view=default -- ./job.sh"},
        linebreak{},
        block{note, "the spec must uniquely identify the uenv. To ensure this, always use a"},
        block{none, "fully qualified spec in the form of name/version:tag, the unique 16 digit id,"},
        block{none, "or sha256 of a uenv. If more than one uenv match the spec, an error message"},
        block{none, "is printed."},
        linebreak{},
        block{xmpl, "run the job.sh script with two images mounted"},
        block{code,   "uenv run prgenv-gnu/24.2:v1,ddt/23.1 -- ./job.sh"},
        linebreak{},
        block{xmpl, "run the job.sh script with two images mounted at specific mount points"},
        block{code,   "uenv run prgenv-gnu/24.2:v1:$SCRATCH/pe,ddt/23.1:/user-tools -- ./job.sh"},
        block{none, "Here the mount point for each image is specified using a ':'."},
        linebreak{},
        block{note, "uenv must be mounted at the mount point for which they were built."},
        block{none, "If mounted at the wrong location, a warning message will be printed, and"},
        block{none, "views will be disabled."},
        linebreak{},
        block{xmpl, "the run command can be used to execute workflow steps with", "separate environments"},
        block{code,   "uenv run gromacs/23.1  -- ./simulation.sh"},
        block{code,   "uenv run paraview/5.11 -- ./render.sh"},
        linebreak{},
        block{xmpl, "run a uenv using a squashfs file:"},
        block{code,   "uenv run --view=tools ./store.squashfs -- nvim main.c"},
        block{code,   "uenv run $SCRATCH/images/gromacs/store.squashfs -- ./simulation.sh"},
        linebreak{},
        block{note, "The path of a squashfs file must be either a relative path"},
        block{none, "(starting with ./ or ../), or an absolute path."},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
