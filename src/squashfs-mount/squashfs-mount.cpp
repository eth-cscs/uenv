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
#include <util/envvars.h>
#include <util/expected.h>
#include <util/shell.h>

#include "terminal.h"

#include <libmount.h>
#include <sys/mount.h>
#include <sys/prctl.h>

util::expected<void, std::string> return_to_user_and_no_new_privs(int uid);
util::expected<void, std::string> unshare_mntns_and_become_root();

/*
   squashfs-mount file:mount file:mount file:mount -- command

   --version --verbose=2, -v, -vv, -vvv */

// do sudo preamble
// do sudo postamble
// update the environment
//  - look for SQFS_MOUNT_ prefixed var
//  - set UENV_MOUNT_LIST etc
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

    CLI::App cli(fmt::format("squashfs-mount {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", verbosity, "enable verbose output");
    cli.add_flag("--version", print_version, "print version");
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

    auto parsed_mounts = uenv::parse_mount_list(raw_mounts);
    if (!parsed_mounts) {
        term::error("invalid sqfs mounts - {}",
                    parsed_mounts.error().message());
        exit(1);
    }
    std::vector<uenv::mount_entry> mounts{};
    mounts.reserve(parsed_mounts->size());
    for (auto mount : parsed_mounts.value()) {
        if (auto v = mount.validate(); !v) {
            term::error("invalid mount {}:{} - {}", mount.sqfs_path,
                        mount.mount_path, v.error());
            exit(1);
        }
        mounts.push_back(std::move(mount));
    }

    const std::string uenv_mount_list = fmt::format(
        "{}", fmt::join(mounts | std::views::transform([](const auto& in) {
                            return fmt::format("{}:{}", in.sqfs_path,
                                               in.mount_path);
                        }),
                        ","));
    spdlog::info("uenv_mount_list {}", uenv_mount_list);
    spdlog::info("commands ['{}']", fmt::join(commands, "', '"));

    //
    // Mount the file systems
    //

    // get the uid before performing any updates to uid
    const uid_t uid = getuid();

    if (auto r = unshare_mntns_and_become_root(); !r) {
        term::error("{}", r.error());
        exit(1);
    }

    if (auto r = uenv::do_mount(mounts); !r) {
        term::error("{}", r.error());
        exit(1);
    }

    if (auto r = return_to_user_and_no_new_privs(uid); !r) {
        term::error("{}", r.error());
        exit(1);
    }

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

    return util::exec(commands, runtime_env.c_env());
}

util::expected<void, std::string> unshare_mntns_and_become_root() {
    if (unshare(CLONE_NEWNS) != 0) {
        // TODO: exit directly with message?
        return util::unexpected{"Failed to unshare the mount namespace"};
    }

    if (mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) != 0) {
        // TODO: exit directly with message?
        return util::unexpected{"Failed to remount \"/\" with MS_SLAVE"};
    }

    // Set real user to root before creating the mount context, otherwise it
    // fails.
    if (setreuid(0, 0) != 0) {
        // TODO: exit directly with message?
        return util::unexpected{"Failed to setreuid\n"};
    }

    // Configure the mount
    // Makes LIBMOUNT_DEBUG=... work.
    mnt_init_debug(0);

    return {};
}

/// set real, effective, saved user id to original user and allow no new
/// priviledges
util::expected<void, std::string> return_to_user_and_no_new_privs(int uid) {
    if (setresuid(uid, uid, uid) != 0) {
        // TODO: exit directly with message?
        return util::unexpected{"setresuid failed"};
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        // TODO: exit directly with message?
        return util::unexpected{"PR_SET_NO_NEW_PRIVS failed"};
    }

    return {};
}
