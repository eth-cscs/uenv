#include "util/expected.h"
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <argparse/argparse.h>
#include <uenv/config.h>
#include <uenv/log.h>
#include <uenv/mount.h>
#include <uenv/mount_kernel.h>
#include <uenv/parse.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/privilege.h>
#include <util/shell.h>

namespace {

namespace {

// the command line arguments
struct squashfs_mount_args {
    bool print_version = false;
    int verbosity = 0;
    std::optional<std::string> raw_mounts;
    std::optional<std::vector<std::string>> commands;
};

} // namespace

// The calling user's real ids, recorded before any privilege change so that
// an exit can give up root.
std::optional<util::ids> caller_ids;

// Exit without exec'ing a command.
//
// The only exits that do not exec are --version and errors, and both are
// reached with root still available from the saved set-user-id. Give it up
// exactly as the exec path does, so that the process ends as an ordinary one
// of the caller: a sanitizer build runs its leak check at exit, and the
// check can only attach to such a process. The result of the drop is not
// checked, as there is nothing left to protect on the way out.
[[noreturn]] void exit_as_caller(int code) {
    if (caller_ids) {
        [[maybe_unused]] auto r = util::drop_privileges(caller_ids.value());
    }
    exit(code);
}

// print a formtted error message and exit with return code 1
template <typename... T>
void error_and_exit(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "{}: {}\n", ::color::red("error"),
               fmt::vformat(fmt, fmt::make_format_args(args...)));
    exit_as_caller(1);
}

} // namespace

// squashfs-mount --sqfs=file:mount[,file:mount] -- cmd [args]
//
// --version --verbose=2, -v, -vv, -vvv
int main(int argc, char** argv, char** envp) {
    //
    // Capture the environment variables
    //

    const auto calling_env = envvars::state(envp);

    // read the ids before performing any privilege/namespace changes
    const auto caller = util::current_ids();
    if (!caller) {
        error_and_exit("{}", caller.error());
    }
    caller_ids = caller->real;

    //
    // Drop the effective uid to the calling user
    //

    // A setuid-root binary starts with euid 0. Everything below - argument
    // parsing, mount-list validation, and above all opening the caller's
    // squashfs images - is then done with the caller's own authority, so the
    // kernel decides at open(2) whether this user may read this image. Root
    // privilege is needed only for the loop device and mount(2) syscalls, and
    // util::become_root() reclaims it there from the saved set-user-id.
    //
    // Only the uid moves. This binary is setuid but not setgid, so the egid
    // and the supplementary groups are already the caller's: the access check
    // requires them, and the exec'd command keeps them. Do not add
    // setegid()/setgroups() here.
    if (caller->effective.gid != caller->real.gid) {
        error_and_exit("refusing to run: squashfs-mount must not be installed "
                       "setgid (egid {} is not the calling gid {})",
                       caller->effective.gid, caller->real.gid);
    }
    // Record whether root can be reclaimed later, while the effective uid
    // still shows it. Only mounting requires root, so a binary that is not
    // installed setuid can still run a command with no images: the check is
    // made when a mount is requested.
    const bool can_become_root =
        caller->effective.uid == 0 || caller->real.uid == 0;
    if (auto r = util::set_effective_uid(caller->real.uid); !r) {
        error_and_exit("{}", r.error());
    }

    //
    // Command line argument parsing
    //

    argparse::command_builder<squashfs_mount_args> builder(
        "squashfs-mount", fmt::format("squashfs-mount {}", UENV_VERSION));
    builder.add_flag({'v', "verbose"}, &squashfs_mount_args::verbosity,
                     "enable verbose output");
    builder.add_flag("version", &squashfs_mount_args::print_version,
                     "print version");
    builder
        .add_option({'s', "sqfs"}, &squashfs_mount_args::raw_mounts,
                    "comma separated list of squashfs files to mount")
        .complete(argparse::completion::custom("mount_list"));
    builder
        .add_rest("commands", &squashfs_mount_args::commands,
                  "the command to run, including with arguments")
        .complete(argparse::completion::command());
    const argparse::program<squashfs_mount_args> cli(std::move(builder));

    const auto command_line = cli.parse(argc, argv);
    if (!command_line) {
        error_and_exit("{}", command_line.error().message);
    }
    if (command_line->help_requested()) {
        fmt::print("{}", command_line->help());
        exit_as_caller(0);
    }
    const auto& args = command_line->globals();
    const bool print_version = args.print_version;
    const int verbosity = args.verbosity;
    const auto& raw_mounts = args.raw_mounts;
    const auto& commands = args.commands;

    //
    // print version and quit if --version flag was used
    //

    if (print_version) {
        fmt::println("{}", UENV_VERSION);
        exit_as_caller(0);
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

    //
    // open the images, still as the calling user
    //

    // This is the access check: the images are opened here, before any
    // privilege is reclaimed, and it is these descriptors - not the paths -
    // that are bound to the loop devices further down.
    std::vector<uenv::opened_image> images;
    if (!mounts.empty()) {
        auto r = uenv::open_images(mounts);
        if (!r) {
            error_and_exit("{}", r.error());
        }
        images = std::move(r).value();
    }

    //
    // mount the squashfs images with the kernel squashfs driver:
    //  * become the real root user and unshare the mount namespace, so that
    //    the images can be mounted;
    //  * attach each image to a loop device and mount it with mount(2);
    //  * drop back to the calling user and disallow gaining new privileges.
    //
    if (!images.empty()) {
        if (!can_become_root) {
            error_and_exit("unable to mount: squashfs-mount is not installed "
                           "setuid root (mode 4755, owned by root), so it "
                           "cannot mount squashfs images");
        }
        if (auto r = util::become_root(); !r) {
            error_and_exit("{}", r.error());
        }
        if (auto r = uenv::unshare_mount_namespace(); !r) {
            error_and_exit("{}", r.error());
        }
        if (auto r = uenv::do_mount(images); !r) {
            error_and_exit("mount failed {}", r.error());
        }
        // the mounts hold their own references to the backing files now: after
        // LOOP_CONFIGURE the kernel keeps a reference, and LO_FLAGS_AUTOCLEAR
        // releases the loop device when the mount goes away.
        images.clear();
    } else {
        spdlog::warn("nothing mounted (no --sqfs flag provided)");
    }

    // Give up root for good before the exec. The saved uid is 0 on both paths,
    // and the real uid is also 0 after mounting.
    if (auto r = util::drop_privileges(caller->real); !r) {
        error_and_exit("{}", r.error());
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
