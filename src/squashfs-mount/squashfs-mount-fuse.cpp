#include "util/expected.h"
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/config.h>
#include <uenv/join_context.h>
#include <uenv/log.h>
#include <uenv/mount.h>
#include <uenv/mount_rootless.h>
#include <uenv/parse.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/shell.h>

// print a formtted error message and exit with return code 1
template <typename... T>
void error_and_exit(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "{}: {}\n", ::color::red("error"),
               fmt::vformat(fmt, fmt::make_format_args(args...)));
    exit(1);
}

bool is_setuid() {
    uid_t euid = geteuid();
    uid_t real_uid = getuid();

    return euid != real_uid;
}

// squashfs-mount --sqfs=file:mount[,file:mount] -- cmd [args]
//
// --version --verbose=2, -v, -vv, -vvv
int main(int argc, char** argv, char** envp) {
    //
    // refuse to use fuse/rootless with setuid
    //
    if (is_setuid()) {
        error_and_exit("Error: attempt to use fuse as setuid.");
    }

    //
    // Capture the environment variables
    //

    const auto calling_env = envvars::state(envp);

    // get the uid/gid before performing any privilege/namespace changes
    const uid_t uid = getuid();
    const gid_t gid = getgid();

    //
    // Command line argument parsing
    //

    bool print_version = false;
    bool tasks_join = false;
    bool fuse_single_threaded = false;
    bool mutable_root = false;
    int verbosity = 1;
    std::optional<std::string> raw_mounts;
    std::optional<std::vector<std::string>> commands;
    std::vector<std::string> tmpfs_arg;
    std::vector<std::string> bind_mounts_arg;

    CLI::App cli(fmt::format("squashfs-mount {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", verbosity, "enable verbose output");
    cli.add_flag("-r,--mutable-root", mutable_root, "mutable root");
    cli.add_flag("--version", print_version, "print version");
    cli.add_flag("--fuse-single", fuse_single_threaded, "fuse single threaded");
    cli.add_flag("--join", tasks_join,
                 "join namespaces of tasks on the same node");
    cli.add_option("-s,--sqfs", raw_mounts,
                   "comma separated list of squashfs files to mount");
    cli.add_option(
        "--tmpfs", tmpfs_arg,
        "space-separated list of tmpfs mounts, each <mount>[:<size>] (size in bytes)");
    cli.add_option("--bind-mount", bind_mounts_arg,
                   "space-separated list of bind mounts, each <src>:<dst>");
    cli.add_option("commands", commands,
                   "the command to run, including with arguments");

    CLI11_PARSE(cli, argc, argv);

    //
    // print version and quit if --version flag was used
    //

    if (print_version) {
        fmt::println("{}", UENV_VERSION);
        return 0;
    }

    //
    // check that required arguments have been set.
    //

    if (!commands) {
        error_and_exit("no command given");
    }

    //
    // set logging level
    //

    // By default there is no logging to the console
    // The level of logging is increased by adding --verbose
    spdlog::level::level_enum console_log_level = spdlog::level::critical;
    if (verbosity == 1) {
        console_log_level = spdlog::level::info;
    } else if (verbosity == 2) {
        console_log_level = spdlog::level::debug;
    } else if (verbosity >= 3) {
        console_log_level = spdlog::level::trace;
    }
    // note: syslog uses level::info to capture key events
    uenv::init_log(console_log_level);

    //
    // validate the mount points
    //

    // with a mutable root, mount points are created as needed, so their
    // existence does not need to be checked up front.
    const bool mount_points_must_exist = !mutable_root;
    uenv::mount_list mounts;
    if (raw_mounts) {
        auto r = uenv::parse_and_validate_mounts(*raw_mounts,
                                                 mount_points_must_exist);
        if (!r) {
            error_and_exit("{}", r.error());
        }
        mounts = r.value();
    }
    const std::string uenv_mount_list =
        fmt::format("{}", fmt::join(mounts, ","));

    spdlog::info("uenv_mount_list {}", uenv_mount_list);
    spdlog::info("commands ['{}']", fmt::join(*commands, "', '"));

    //
    // validate the tmpfs and bind mount arguments
    //

    auto tmpfs = uenv::parse_tmpfs_and_validate(tmpfs_arg, mutable_root);
    if (!tmpfs) {
        error_and_exit("failed to parse tmpfs: {}", tmpfs.error());
    }
    auto bind_mounts =
        uenv::parse_bindmounts_and_validate(bind_mounts_arg, mutable_root);
    if (!bind_mounts) {
        error_and_exit("failed to parse bind mounts: {}", bind_mounts.error());
    }

    if (!mounts.empty() || !tmpfs->empty() || !bind_mounts->empty() ||
        mutable_root) {
        auto join_ctx = uenv::local_join_context(calling_env, tasks_join);
        if (!join_ctx) {
            error_and_exit("{}", join_ctx.error());
        }
        spdlog::trace("joining {} task(s) on this node with tag '{}'",
                      join_ctx->ntasks, join_ctx->tag);

        if (auto r = uenv::rootless::mount_and_join_ns(
                join_ctx->tag, join_ctx->ntasks, mounts, *bind_mounts, *tmpfs,
                fuse_single_threaded, uid, gid, mutable_root);
            !r) {
            error_and_exit("mount failed {}", r.error());
        }
    } else {
        spdlog::warn("nothing mounted (no --sqfs, --tmpfs or --bind-mount flag "
                     "provided)");
    }

    //
    // Generate the runtime environment variables
    //
    envvars::state runtime_env{};

    // forward all environment variables not prefixed with SQFSMNT_FWD_
    for (auto& [name, v] : calling_env.variables()) {
        if (!name.starts_with("SQFSMNT_FWD_")) {
            // use forward instead of set, becase set drops environment
            // variables that do not have valid POSIX compliant names.
            // In this context we need to tolerate all possible names to fully
            // reproduce the calling environment.
            // For example: bash function exports do not follow the POSIX
            // standard.

            runtime_env.forward(name, v);
        }
    }
    // add the forwarded variables in a second loop, in case a variable with the
    // same name was already in the calling environment.
    for (auto& [name, v] : calling_env.variables()) {
        if (name.starts_with("SQFSMNT_FWD_")) {
            // use set, which will still drop environment variables with invalid
            // names, because an invalid name in this context is certainly a bug
            // in the caller.
            runtime_env.set(name.substr(12), v);
        }
    }

    runtime_env.set("UENV_MOUNT_LIST", uenv_mount_list);

    auto cenv = runtime_env.c_env();
    auto error = util::exec(*commands, cenv);

    // it is always an error if this code is executed, because that implies that
    // execvp failed.
    envvars::c_env_free(cenv);
    error_and_exit("{}", error.message);

    return error.rcode;
}
