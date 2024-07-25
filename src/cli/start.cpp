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

#include "start.h"
#include "uenv.h"

namespace uenv {

void start_help() {
    fmt::println("start help");
}

void start_args::add_cli(CLI::App& cli, global_settings& settings) {
    auto* start_cli = cli.add_subcommand("start", "start a uenv session");
    start_cli->add_option("-v,--view", view_description,
                          "comma separated list of views to load");
    start_cli
        ->add_option("uenv", uenv_description,
                     "comma separated list of uenv to mount")
        ->required();
    start_cli->callback([&settings]() { settings.mode = uenv::mode_start; });
}

int start(const start_args& args, const global_settings& globals) {
    fmt::println("[log] start with options {}", args);
    const auto env =
        concretise_env(args.uenv_description, args.view_description);

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

    // find the current shell (zsh, bash, etc)
    auto shell = util::current_shell();
    if (!shell) {
        fmt::print("[error] unable to determine the current shell because {}\n",
                   shell.error());
    }
    fmt::println("[log] shell found: {}", shell->string());

    commands.push_back("--");
    commands.push_back(shell->string());

    fmt::print("[log] exec {}\n", fmt::join(commands, " "));
    return util::exec(commands);
}

} // namespace uenv
