#include <filesystem>
#include <functional>
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
#include <uenv/ns_join.h>
#include <uenv/parse.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/shell.h>

#ifdef UENV_FUSE_MOUNT
#include <uenv/rootless.h>
#endif

namespace fs = std::filesystem;

// print a formtted error message and exit with return code 1
template <typename... T>
void error_and_exit(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "{}: {}\n", ::color::red("error"),
               fmt::vformat(fmt, fmt::make_format_args(args...)));
    exit(1);
}

// synchronize tasks on the same node: the winner sets up the sandbox and
// performs the mounts, the rest join the winner's namespaces.
template <typename R>
void mount_and_join_ns(bool tasks_join, int ntasks, R&& mount) {
    uenv::join_t join;

    if (tasks_join) {
        auto r = uenv::join_begin(join, "tag");
        if (!r) {
            error_and_exit("join_begin failed {}", r.error());
        }
    }

    if (!tasks_join || join.winner_p) {
        auto r = mount();
        if (!r) {
            error_and_exit("mount failed {}", r.error());
        }
    } else {
        auto r = uenv::namespaces_join(join.shared->winner_pid);
        if (!r) {
            error_and_exit("namespaces_join failed {}", r.error());
        }
    }

    if (tasks_join) {
        uenv::join_end(join, ntasks, std::nullopt);
    }
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
#ifdef UENV_FUSE_MOUNT
    if (is_setuid()) {
        error_and_exit("Error: attempt to use fuse as setuid.");
    }
#endif

    //
    // Capture the environment variables
    //

    const auto calling_env = envvars::state(envp);

    // get the uid/gid before performing any privilege/namespace changes
    const uid_t uid = getuid();
#ifdef UENV_FUSE_MOUNT
    const gid_t gid = getgid();
#endif

    //
    // Command line argument parsing
    //

    bool print_version = false;
    bool mutable_root = false;
    bool tasks_join = false;
    bool fuse_st = false;
    int verbosity = 1;
    std::optional<std::string> raw_mounts;
    std::optional<std::vector<std::string>> commands;
    std::vector<std::string> tmpfs_arg;
    std::vector<std::string> bind_mounts_arg;

    CLI::App cli(fmt::format("squashfs-mount {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", verbosity, "enable verbose output");
    cli.add_flag("-r,--mutable-root", mutable_root, "mutable root");
    cli.add_flag("--version", print_version, "print version");
    cli.add_flag("--join", tasks_join,
                 "join namespaces of tasks on the same node");
    cli.add_flag("--fuse-single", fuse_st,
                 "fuse single threaded (ignored unless built with fuse)");
    cli.add_option("-s,--sqfs", raw_mounts,
                   "comma separated list of squashfs files to mount");
    cli.add_option("--tmpfs", tmpfs_arg, "tmpfs mount point[:size]");
    cli.add_option("--bind-mount", bind_mounts_arg, "bind mount <src>:<dst>");
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
    // validate the mount points, tmpfs and bind mount arguments
    //

    // when using a mutable root, mount points are created, skip test for
    // existence
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

    auto tmpfs = uenv::parse_tmpfs(tmpfs_arg);
    if (!tmpfs) {
        auto err = tmpfs.error();
        error_and_exit("failed to parse tmpfs msg=`{}` detail=`{}` "
                       "description=`{}`, input=`{}`",
                       err.message(), err.detail, err.description, err.input);
    }
    auto bind_mounts = uenv::parse_bindmounts(bind_mounts_arg);
    if (!bind_mounts) {
        auto err = bind_mounts.error();
        error_and_exit("failed to parse bind mounts msg=`{}` detail=`{}` "
                       "description=`{}`, input=`{}`",
                       err.message(), err.detail, err.description, err.input);
    }

    //
    // Select the implementations of the three points where the setuid
    // (kernel squashfs driver) and rootless (squashfuse in a user namespace)
    // builds differ:
    //  * setup_sandbox: unshare namespaces and gain the privileges required
    //    to perform the mounts below.
    //  * mount_sqfs: mount a single squashfs image.
    //  * drop_privileges: return to the unprivileged calling user and
    //    disallow gaining any new privileges.
    // Everything else -- argument parsing, tmpfs, bind mounts, forwarding the
    // environment, and exec'ing the command -- is shared unconditionally.
    //
    using hook = std::function<util::expected<void, std::string>()>;
    using mount_hook = std::function<util::expected<void, std::string>(
        const uenv::mount_pair&)>;

#ifdef UENV_FUSE_MOUNT
    hook setup_sandbox = []() {
        return uenv::rootless::unshare_mount_map_root();
    };
    mount_hook mount_sqfs =
        [fuse_st](const uenv::mount_pair& entry) {
            return uenv::rootless::do_sqfs_ll_mount(entry, fuse_st);
        }
    ;
    hook exit_sandbox = [uid, gid]() {
        return uenv::rootless::exit_sandbox(uid, gid);
    };
#else
    hook setup_sandbox = []() { return uenv::unshare_and_become_root(); };
    mount_hook mount_sqfs = [](const uenv::mount_pair& entry) {
        return uenv::do_mount({entry});
    };
    // for setuid, priviledges are always dropped and for all tasks
    // therefore this hook is empty
    hook exit_sandbox = []() -> util::expected<void, std::string> {
        return {};
    };
#endif

    //
    // mount: mutable root, bind mounts, tmpfs, then the squashfs images.
    //
    auto do_mounts = [mutable_root, &bind_mounts, &tmpfs, &mounts,
                      &mount_sqfs]() -> util::expected<void, std::string> {
        if (mutable_root) {
            if (auto r = uenv::make_mutable_root(); !r) {
                return r;
            }
        }

        for (auto& entry : *bind_mounts) {
            if (mutable_root) {
                fs::create_directories(entry.dst);
            }
            if (auto r = uenv::bind_mount(entry.src, entry.dst); !r) {
                return r;
            }
        }

        for (auto& entry : *tmpfs) {
            if (mutable_root) {
                fs::create_directories(entry.mount);
            }
            if (auto r = uenv::mount_tmpfs(entry.mount, entry.size); !r) {
                return r;
            }
        }

        for (auto& entry : mounts) {
            if (mutable_root) {
                fs::create_directories(entry.mount);
            }
            if (auto r = mount_sqfs(entry); !r) {
                return r;
            }
        }

        return {};
    };

    const bool has_work =
        !mounts.empty() || !tmpfs->empty() || !bind_mounts->empty();

    if (has_work) {
        auto pipeline =
            [&setup_sandbox, &do_mounts,
             &exit_sandbox]() -> util::expected<void, std::string> {
            if (auto r = setup_sandbox(); !r) {
                return r;
            }
            if (auto r = do_mounts(); !r) {
                return r;
            }
            return exit_sandbox();
        };

        int ntasks = std::stoi(
            calling_env.get("SLURM_STEP_TASKS_PER_NODE").value_or("1"));
        /// SLURM only at the moment
        spdlog::trace("SLURM_STEP_TASKS_PER_NODE: {}", ntasks);
        mount_and_join_ns(tasks_join, ntasks, pipeline);
    } else {
        spdlog::warn("nothing mounted (no --sqfs, --tmpfs or --bind-mount flag "
                     "provided)");
    }
#ifndef UENV_FUSE_MOUNT
    // the setuid binary always starts with an elevated effective uid, so
    // it has to be dropped even when there is nothing to mount and for all
    // tasks
    if (auto r = uenv::return_to_user_and_no_new_privs(uid); !r) {
        error_and_exit("{}", r.error());
    }
#endif

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
