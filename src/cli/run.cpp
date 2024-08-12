// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <util/expected.h>
#include <util/shell.h>

#include "run.h"
#include "uenv.h"

namespace uenv {

void run_help() {
    fmt::println("run help");
}

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
    run_cli->callback([&settings]() { settings.mode = uenv::mode_run; });
}

int run(const run_args& args, const global_settings& globals) {
    fmt::println("[log] run with options {}", args);
    const auto env = concretise_env(args.uenv_description,
                                    args.view_description, globals.repo);

    if (!env) {
        fmt::print("[error] {}\n", env.error());
        return 1;
    }

    // generate the environment variables to set
    auto env_vars = uenv::getenv(*env);

    if (auto rval = uenv::setenv(env_vars, "SQFSMNT_FWD_"); !rval) {
        fmt::print("[error] setting environment variables {}\n", rval.error());
        return 1;
    }

    // generate the mount list
    std::vector<std::string> commands = {"squashfs-mount"};
    for (auto e : env->uenvs) {
        commands.push_back(fmt::format("{}:{}", e.second.sqfs_path.string(),
                                       e.second.mount_path));
    }

    commands.push_back("--");
    commands.insert(commands.end(), args.commands.begin(), args.commands.end());

    fmt::print("[log] exec {}\n", fmt::join(commands, " "));
    return util::exec(commands);
}

} // namespace uenv
