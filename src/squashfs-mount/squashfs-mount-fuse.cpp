#include "util/expected.h"
#include <optional>
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
#include <uenv/rootless.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/proc_barrier.h>
#include <util/setns.h>
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

// synchronize tasks on the same node: the elected leader performs the
// mounts, the rest join the leader's namespaces.
template <typename R>
void mount_and_join_ns(bool tasks_join, int ntasks, R&& pipeline) {

    if (!tasks_join) {
        // no tasks need to join, just call pipeline
        auto r = pipeline();
        if (!r) {
            error_and_exit("mount failed {}", r.error());
        }
        return;
    }

    auto barrier = util::proc_barrier::create("tag", ntasks);
    if (!barrier) {
        error_and_exit("unable to create the barrier {}", barrier.error());
    }

    if (barrier->is_leader()) {
        auto r = pipeline();
        if (!r) {
            error_and_exit("mount failed {}", r.error());
        }
        // publish our pid and release the tasks blocked in the barrier so
        // they can join our namespaces.
        if (auto rr = barrier->ready(); !rr) {
            error_and_exit("barrier ready failed {}", rr.error());
        }
    } else {
        auto r = util::namespaces_join(barrier->leader_pid(), {"user", "mnt"});
        if (auto sr = barrier->signal_done(); !sr) {
            spdlog::warn("barrier signal_done failed: {}", sr.error());
        }
        if (!r) {
            error_and_exit("namespaces_join failed {}", r.error());
        }
    }

    if (auto r = barrier->end(); !r) {
        spdlog::warn("barrier end failed: {}", r.error());
    }
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
    int verbosity = 1;
    std::optional<std::string> raw_mounts;
    std::optional<std::vector<std::string>> commands;

    CLI::App cli(fmt::format("squashfs-mount {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", verbosity, "enable verbose output");
    cli.add_flag("--version", print_version, "print version");
    cli.add_flag("--join", tasks_join,
                 "join namespaces of tasks on the same node");
    cli.add_option("-s,--sqfs", raw_mounts,
                   "comma separated list of squashfs files to mount");
    cli.add_option("commands", commands,
                   "the command to run, including with arguments");
    cli.add_flag("--fuse-single", fuse_single_threaded, "fuse single threaded");

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
    spdlog::level::level_enum console_log_level = spdlog::level::off;
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

    uenv::mount_list mounts;
    if (raw_mounts) {
        auto r = uenv::parse_and_validate_mounts(*raw_mounts);
        if (!r) {
            error_and_exit("{}", r.error());
        }
        mounts = r.value();
    }
    const std::string uenv_mount_list =
        fmt::format("{}", fmt::join(mounts, ","));

    spdlog::info("uenv_mount_list {}", uenv_mount_list);
    spdlog::info("commands ['{}']", fmt::join(*commands, "', '"));

    if (!mounts.empty()) {
        auto pipeline = [mounts, fuse_single_threaded, uid,
                         gid]() -> util::expected<void, std::string> {
            if (auto r = uenv::rootless::unshare_mount_map_root(); !r) {
                return r;
            }
            for (auto& entry : mounts) {
                if (auto r = uenv::rootless::do_sqfs_ll_mount(entry, fuse_single_threaded);
                    !r) {
                    return r;
                }
            }
            return uenv::rootless::exit_sandbox(uid, gid);
        };

        int ntasks = std::stoi(
            calling_env.get("SLURM_STEP_TASKS_PER_NODE").value_or("1"));
        /// SLURM only at the moment
        spdlog::trace("SLURM_STEP_TASKS_PER_NODE: {}", ntasks);
        // join only if > 1 task
        tasks_join = ntasks > 1 ? tasks_join : false;
        mount_and_join_ns(tasks_join, ntasks, pipeline);
    } else {
        spdlog::warn("nothing mounted (no --sqfs flag provided)");
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
