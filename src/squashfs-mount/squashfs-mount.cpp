#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/config.h>
#include <uenv/log.h>
#include <uenv/mount.h>
#include <uenv/parse.h>
#include <util/expected.h>

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
int main(int argc, char** argv) {

    // variables set by cli argument parser
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

    // print version and quit if --version flag was used
    if (print_version) {
        fmt::println("{}", UENV_VERSION);
        return 0;
    }

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

    // parse and validate the mounts passed with the --sqfs argument.
    // the values in mounts are fully validated, and can be used without further
    // validation.
    auto mounts = uenv::parse_mount_list(raw_mounts);
    if (!mounts) {
        term::error("invalid sqfs mounts - {}", mounts.error().message());
        exit(1);
    }
    for (auto const& mount : mounts.value()) {
        if (auto v = mount.validate(); !v) {
            term::error("invalid mount {}:{} - {}", mount.sqfs_path,
                        mount.mount_path, v.error());
            exit(1);
        }
    }

    spdlog::info("mounts {}", raw_mounts);
    spdlog::info("commands: ['{}']", fmt::join(commands, "', '"));

    return 0;
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
