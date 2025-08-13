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

void return_to_user_and_no_new_privs(int uid);
void unshare_mntns_and_become_root();

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
    std::optional<std::string> raw_mounts;
    std::optional<std::vector<std::string>> commands;

    CLI::App cli(fmt::format("squashfs-mount {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", verbosity, "enable verbose output");
    cli.add_flag("--version", print_version, "print version");
    cli.add_option("-s,--sqfs", raw_mounts,
                   "comma separated list of uenv to mount");
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
    // do this manually instead of using the required() option in CLI11, in
    // order to be able to handle `squashfs-mount --version` withouth complaints
    // that `--sqfs` and commands were not provided.
    //
    if (!raw_mounts) {
        error_and_exit("the --sqfs option must be set");
    }
    if (!commands) {
        error_and_exit("the commands must be provided");
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

    auto mounts = uenv::parse_and_validate_mounts(*raw_mounts);
    if (!mounts) {
        error_and_exit("{}", mounts.error());
    }
    const std::string uenv_mount_list =
        fmt::format("{}", fmt::join(mounts.value(), ","));

    spdlog::info("uenv_mount_list {}", uenv_mount_list);
    spdlog::info("commands ['{}']", fmt::join(*commands, "', '"));

    //
    // Mount the file systems
    //

    // get the uid before performing any updates to uid
    const uid_t uid = getuid();

    unshare_mntns_and_become_root();

    if (auto r = uenv::do_mount(mounts.value()); !r) {
        error_and_exit("{}", r.error());
    }

    return_to_user_and_no_new_privs(uid);

    //
    // Generate the runtime environment variables
    //

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

    auto cenv = runtime_env.c_env();
    auto error = util::exec(*commands, cenv);

    // it is always an error if this code is executed, because that implies that
    // execvp failed.
    envvars::c_env_free(cenv);
    error_and_exit("{}", error.message);

    return error.rcode;
}

void unshare_mntns_and_become_root() {
    if (unshare(CLONE_NEWNS) != 0) {
        error_and_exit("Failed to unshare the mount namespace");
    }

    if (mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) != 0) {
        error_and_exit("Failed to remount \"/\" with MS_SLAVE");
    }

    // Set real user to root before creating the mount context, otherwise it
    // fails.
    if (setreuid(0, 0) != 0) {
        error_and_exit("Failed to setreuid");
    }

    // Configure the mount
    // Makes LIBMOUNT_DEBUG=... work.
    mnt_init_debug(0);
}

// set real, effective, saved user id to original user and allow no new
// priviledges
void return_to_user_and_no_new_privs(int uid) {
    if (setresuid(uid, uid, uid) != 0) {
        error_and_exit("setresuid failed");
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        error_and_exit("PR_SET_NO_NEW_PRIVS failed");
    }
}
