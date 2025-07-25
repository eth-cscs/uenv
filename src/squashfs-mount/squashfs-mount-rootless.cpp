#include "squashfs-mount/rootless.h"
#include <ranges>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/config.h>
#include <uenv/log.h>
#include <uenv/mount.h>
#include <uenv/parse.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/shell.h>

#include <libmount.h>
#include <sys/mount.h>
#include <sys/prctl.h>

// print a formtted error message and exit with return code 1
template <typename... T>
void error_and_exit(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "{}: {}\n", ::color::red("error"),
               fmt::vformat(fmt, fmt::make_format_args(args...)));
    exit(1);
}

// squashfs-mount --sqfs=file:mount[,file:mount] -- cmd [args]
//
// --version --verbose=2, -v, -vv, -vvv
int main(int argc, char** argv, char** envp) {
    //
    // Capture the environment variables
    //

    const auto calling_env = envvars::state(envp);

    //
    // Command line argument parsing
    //

    bool print_version = false;
    int verbosity = 1;
    std::string raw_mounts;
    std::vector<std::string> commands;
    std::vector<std::string> tmpfs;

    CLI::App cli(fmt::format("squashfs-mount {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", verbosity, "enable verbose output");
    cli.add_option("--tmpfs", tmpfs, "tmpfs");
    cli.add_option("-s,--sqfs", raw_mounts,
                   "comma separated list of uenv to mount")
        ->required();
    cli.add_option("commands", commands,
                   "the command to run, including with arguments")
        ->required();
    cli.add_option("args", commands, "comma separated list of uenv to mount");

    CLI11_PARSE(cli, argc, argv);

    //
    // print version and quit if --version flag was used
    //

    if (print_version) {
        fmt::println("{}", UENV_VERSION);
        return 0;
    }

    //
    // set logging level
    //

    // By default there is no logging to the console
    // The level of logging is increased by adding --verbose
    spdlog::level::level_enum console_log_level = spdlog::level::off;
    if (verbosity == 1) {
        console_log_level = spdlog::level::info;
    } else if (verbosity == 2) {
        console_log_level = spdlog::level::debug;
    } else if (verbosity >= 3) {
        console_log_level = spdlog::level::trace;
    }

    // note: syslog uses level::info to capture key events
    uenv::init_log(console_log_level, spdlog::level::info);

    //
    // validate the mount points
    //

    auto mounts = uenv::parse_and_validate_mounts(raw_mounts);
    if (!mounts) {
        error_and_exit("{}", mounts.error());
    }
    const std::string uenv_mount_list =
        fmt::format("{}", fmt::join(mounts.value(), ","));

    spdlog::info("uenv_mount_list {}", uenv_mount_list);
    spdlog::info("commands ['{}']", fmt::join(commands, "', '"));

    const uid_t uid = getuid();
    const uid_t gid = getgid();

    //
    // Squashfuse mount
    //
    if (auto r = uenv::unshare_mount_map_root(); !r) {
        spdlog::error("fake-root failed {}", r.error());
        return 1;
    }

    for (auto mount : mounts.value()) {
        if (auto r = do_sqfs_mount(mount); !r) {
            spdlog::error("failed to mount {}", r.error());
            return 1;
        }
    }

    envvars::state runtime_env{};

    // forward all environment variables not prefixed with SQFSMNT_FWD_
    for (auto& [name, v] : calling_env.variables()) {
        if (!name.starts_with("SQFSMNT_FWD_")) {
            runtime_env.set(name, v);
        }
    }
    // add the forwarded variables in a second loop, in case a variable with the
    // same name was already in the calling environment.
    for (auto& [name, v] : calling_env.variables()) {
        if (name.starts_with("SQFSMNT_FWD_")) {
            runtime_env.set(name.substr(12), v);
        }
    }

    // add UENV environment variables
    runtime_env.set("UENV_MOUNT_LIST", uenv_mount_list);

    // exit fake-root
    if ( auto r = uenv::map_effective_user(uid, gid); !r) {
        spdlog::error("failed {}", r.error());
        return 1;
    }

    auto rcode = util::exec(commands, runtime_env.c_env());

    return rcode;
}

