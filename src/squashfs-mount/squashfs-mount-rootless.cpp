#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <uenv/rootless.h>
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

// #include <libmount.h>
#include <sys/mount.h>
#include <sys/prctl.h>

// print a formtted error message and exit with return code 1
template <typename... T>
void error_and_exit(fmt::format_string<T...> fmt, T&&... args) {
    fmt::print(stderr, "{}: {}\n", ::color::red("error"),
               fmt::vformat(fmt, fmt::make_format_args(args...)));
    exit(1);
}

namespace fs = std::filesystem;

void setup_ns_and_mount(bool mutable_root, bool fuse_st,
                        std::vector<std::string> tmpfs_str,
                        std::vector<std::string> bind_mounts_str,
                        uenv::mount_list mounts) {

    const uid_t uid = getuid();
    const uid_t gid = getgid();

    // unshare mount ns, enter fake-root
    if (auto r = uenv::unshare_mount_map_root(); !r) {
        error_and_exit("fake-root failed {}", r.error());
    }

    // create mutable root
    if (mutable_root) {
        if (auto r = uenv::make_mutable_root(); !r) {
            error_and_exit("mutable-root failed {}", r.error());
        }
    }

    // tmps
    auto tmpfs = uenv::parse_tmpfs(tmpfs_str);
    if (!tmpfs) {
        auto err = tmpfs.error();
        error_and_exit("failed to parse tmpfs msg=`{}` detail=`{}` "
                       "description=`{}`, input=`{}`",
                       err.message(), err.detail, err.description, err.input);
    }
    // bind mounts
    auto bind_mounts = uenv::parse_bindmounts(bind_mounts_str);
    if (!bind_mounts) {
        auto err = bind_mounts.error();
        error_and_exit("failed to parse tmpfs msg=`{}` detail=`{}` "
                       "description=`{}`, input=`{}`",
                       err.message(), err.detail, err.description, err.input);
    }
    for (auto entry : bind_mounts.value()) {
        if (mutable_root) {
            fs::create_directories(entry.dst);
        }
        auto r = uenv::bind_mount(entry.src, entry.dst);
        if (!r) {
            error_and_exit("bindmount failed {}", r.error());
        }
    }

    for (auto& entry : tmpfs.value()) {
        if (mutable_root) {
            fs::create_directories(entry.mount);
        }
        auto r = uenv::mount_tmpfs(entry.mount, entry.size);
        if (!r) {
            error_and_exit("tmpfs creation failed {}", r.error());
        }
    }

    for (auto mount : mounts) {
        if (mutable_root) {
            fs::create_directories(mount.mount);
        }
        if (auto r = do_sqfs_mount(mount, fuse_st); !r) {
            error_and_exit("failed to mount {}", r.error());
        }
    }

    // exit fake-root
    if (auto r = uenv::map_effective_user(uid, gid); !r) {
        error_and_exit("failed {}", r.error());
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        error_and_exit("PR_SET_NO_NEW_PRIVS failed");
    }
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

    int verbosity = 1;
    bool mutable_root = false;
    bool fuse_st = false;
    bool print_version = false;
    bool tasks_join = false;
    std::string raw_mounts;
    std::vector<std::string> commands;
    std::vector<std::string> tmpfs_str;
    std::vector<std::string> bind_mounts_str;

    CLI::App cli(fmt::format("squashfs-mount {}", UENV_VERSION));
    cli.add_flag("-v,--verbose", verbosity, "enable verbose output");
    cli.add_flag("-r,--mutable-root", mutable_root, "mutable root");
    cli.add_flag("--fuse-single", fuse_st, "fuse single threaded");
    cli.add_flag("--version", print_version, "print version");
    cli.add_flag("--join", tasks_join, "join");
    cli.add_option("--tmpfs", tmpfs_str, "tmpfs[:size]");
    cli.add_option("--bind-mount", bind_mounts_str, "bind_mounts <src>:<dst>");
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

    // when using a mutable root, mount_points are created, skip test for
    // existence
    bool mount_points_must_exist = !mutable_root;
    uenv::mount_list mounts;
    if (cli.get_option("--sqfs")->count() > 0) {
        auto r = uenv::parse_and_validate_mounts(raw_mounts,
                                                 mount_points_must_exist);
        if (!r) {
            error_and_exit("{}", r.error());
        }
        mounts = r.value();
    }
    const std::string uenv_mount_list =
        fmt::format("{}", fmt::join(mounts, ","));

    spdlog::info("uenv_mount_list {}", uenv_mount_list);
    spdlog::info("commands ['{}']", fmt::join(commands, "', '"));

    uenv::join_t join;

    if (tasks_join) {
        uenv::join_begin(join, "tag");
    }

    if (!tasks_join || join.winner_p) {
        setup_ns_and_mount(mutable_root, fuse_st, tmpfs_str, bind_mounts_str,
                           mounts);
    } else {
        uenv::namespaces_join(join.shared->winner_pid);
    }

    if (tasks_join) {
        auto env_vars = calling_env.variables();
        int ntasks = 1;
        /// SLURM only at the moment
        if (auto f = env_vars.find("SLURM_STEP_TASKS_PER_NODE");
            f != env_vars.end()) {
            ntasks = std::stoi(f->second);
            spdlog::trace("SLURM_STEP_TASKS_PER_NODE: {}", ntasks);
        }
        uenv::join_end(join, ntasks, std::nullopt);
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

    auto cenv = runtime_env.c_env();
    auto error = util::exec(commands, cenv);

    // it is always an error if this code is executed, because that implies that
    // execvp failed.
    envvars::c_env_free(cenv);
    error_and_exit("{}", error.message);

    return error.rcode;
}
