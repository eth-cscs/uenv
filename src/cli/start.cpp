// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <util/expected.h>
#include <util/shell.h>

#include "help.h"
#include "start.h"
#include "uenv.h"

namespace uenv {

std::string start_footer();

void start_args::add_cli(CLI::App& cli,
                         [[maybe_unused]] global_settings& settings) {
    auto* start_cli = cli.add_subcommand("start", "start a uenv session");
    start_cli->add_option("-v,--view", view_description,
                          "comma separated list of views to load");
    start_cli
        ->add_option("uenv", uenv_description,
                     "comma separated list of uenv to mount")
        ->required();
    start_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::start; });
    start_cli->footer(start_footer);
}

int start(const start_args& args,
          [[maybe_unused]] const global_settings& globals) {
    spdlog::info("start with options {}", args);
    const auto env = concretise_env(args.uenv_description,
                                    args.view_description, globals.repo);

    if (!env) {
        spdlog::error("{}", env.error());
        return 1;
    }

    // generate the environment variables to set
    auto env_vars = uenv::getenv(*env);

    if (auto rval = uenv::setenv(env_vars, "SQFSMNT_FWD_"); !rval) {
        spdlog::error("setting environment variables {}", rval.error());
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
        spdlog::error("unable to determine the current shell because {}",
                      shell.error());
        return 1;
    }
    spdlog::info("shell found: {}", shell->string());

    commands.push_back("--");
    commands.push_back(shell->string());

    return util::exec(commands);
}

std::string start_footer() {
    using enum help::block::admonition;
    using help::lst;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Start a new shell with a uenv environment. The shell will be",
                          fmt::format("the default shell set using the SHELL envronment variable ({}).", lst("echo $SHELL"))},
        help::linebreak{},
        help::block{note,
                "the uenv must have been pulled before it can be used. See the list",
                fmt::format("of available uenv using {}.", lst("uenv image ls")),
                "If using a path to a squashfs file, you need to have read rights in",
                "the path where the file is stored.",
        },
        help::linebreak{},
        help::block{xmpl, "start a uenv"},
        help::block{code,   "uenv start prgenv-gnu/24.7:v3"},
        help::block{none, "use the full name/version:tag format to disambiguate fully the image "},
        help::linebreak{},
        help::block{info, "uenv will mount the image at the correct location, which for most uenv",
                          "is /user-enviroment."},
        help::linebreak{},
        help::block{xmpl, fmt::format("start an image built for the system daint using {}", lst("@daint"))},
        help::block{code,   "uenv start prgenv-gnu/24.7:v1@daint"},
        help::linebreak{},
        help::block{xmpl, "use the @ symbol to specify a target system name"},
        help::block{code,   "uenv start prgenv-gnu@todi"},
        help::block{none, "this feature is useful when using images that were built for a different system",
                          "than the one you are currently working on."},
        help::linebreak{},
        help::block{xmpl, "two uenv images can be used at the same time"},
        help::block{code,   "uenv start prgenv-gnu/24.7:v3,editors/24.7:v1"},
        help::linebreak{},
        help::block{info, "to start two uenv at the same time, they must be mounted at different mount.",
                          "points. uenv provided by CSCS are designed to be mounted at two locations:",
                          fmt::format("  - {} programming environments and applications", help::lst("/user-enviroment")),
                          fmt::format("  - {} tools like debuggers and profilers that are used", help::lst("/user-tools")),
                                      "    alongside PE and applications"},
        help::linebreak{},
        help::block{xmpl, "example of using the full specification:"},
        help::block{code,   "uenv start prgenv-gnu/24.7:v3:/user-environemnt,editors/24.7:v1:user-tools \\",
                            "           --view=prgenv-gnu:default,editors:modules"},
        help::linebreak{},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}
} // namespace uenv
