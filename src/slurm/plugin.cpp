#include <charconv>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/log.h>
#include <uenv/mount.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/envvars.h>

#include "config.hpp"

extern "C" {
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
}

namespace uenv {

util::expected<void, std::string>
do_mount(const std::vector<mount_entry>& mount_entries);

void set_log_level(const envvars::state& env) {
    // use warn as the default log level
    auto log_level = spdlog::level::warn;
    bool invalid_env = false;

    // check the environment variable UENV_LOG_LEVEL
    auto log_level_str = env.get("UENV_LOG_LEVEL");
    if (log_level_str) {
        int lvl;
        auto [ptr, ec] = std::from_chars(
            log_level_str->c_str(),
            log_level_str->c_str() + log_level_str->size(), lvl);

        if (ec == std::errc()) {
            if (lvl == 1) {
                log_level = spdlog::level::info;
            } else if (lvl == 2) {
                log_level = spdlog::level::debug;
            } else if (lvl > 2) {
                log_level = spdlog::level::trace;
            }
        } else {
            invalid_env = true;
        }
    }
    uenv::init_log(log_level, spdlog::level::info);
    if (invalid_env) {
        spdlog::warn(fmt::format("UENV_LOG_LEVEL invalid value '{}' -- "
                                 "expected a value between 0 and 3",
                                 log_level_str));
    }
}

} // namespace uenv

//
// Forward declare the implementation of the plugin callbacks.
//

namespace impl {
int slurm_spank_init(spank_t sp, int ac, char** av);
int slurm_spank_init_post_opt(spank_t sp, int ac, char** av);
} // namespace impl

//
// Implement the SPANK plugin C interface.
//

extern "C" {

extern const char plugin_name[] = "uenv-mount";
extern const char plugin_type[] = "spank";
#ifdef SLURM_VERSION_NUMBER
extern const unsigned int plugin_version = SLURM_VERSION_NUMBER;
#endif
extern const unsigned int spank_plugin_version = 1;

// Called from both srun and slurmd.
int slurm_spank_init(spank_t sp, int ac, char** av) {
    return impl::slurm_spank_init(sp, ac, av);
}

int slurm_spank_init_post_opt(spank_t sp, int ac, char** av) {
    return impl::slurm_spank_init_post_opt(sp, ac, av);
}

} // extern "C"

//
// Implementation
//
namespace impl {
struct arg_pack {
    std::optional<std::string> uenv_description;
    std::optional<std::string> view_description;
    std::optional<std::string> repo_description;
};

static arg_pack args{};

/// wrapper spank_getenv : for use in the remote context
std::optional<std::string> getenv_wrapper(spank_t sp, const char* var) {
    const int len = 1024;
    char buf[len];

    const auto ret = spank_getenv(sp, var, buf, len);

    if (ret == ESPANK_ENV_NOEXIST) {
        return std::nullopt;
    }

    if (ret == ESPANK_SUCCESS) {
        return std::string{buf};
    }

    slurm_spank_log("getenv failed");
    throw ret;
}

util::expected<std::vector<uenv::mount_entry>, std::string>
validate_uenv_mount_list(std::string mount_var) {
    auto mount_list = uenv::parse_mount_list(mount_var);
    if (!mount_list) {
        return util::unexpected(mount_list.error().message());
    }

    for (auto& entry : *mount_list) {
        // NOTE: the validate step is important because it checks both
        // that the paths exist AND that the user has permission
        // to read the squashfs image. Weakening this check would possibly
        // let users mount images to which they don't have access.
        if (auto valid = entry.validate(); !valid) {
            return util::unexpected(valid.error());
        }
    }

    return *mount_list;
}

static spank_option uenv_arg{
    (char*)"uenv",
    (char*)"<file>[:mount-point][,<file:mount-point>]*",
    (char*)"A comma seprated list of file and mountpoint, default mount-point "
           "is " DEFAULT_MOUNT_POINT,
    1, // requires an argument
    0, // plugin specific value to pass to the callback (unnused)
    [](int val, const char* optarg, int remote) -> int {
        slurm_verbose("uenv: val:%d optarg:%s remote:%d", val, optarg, remote);
        args.uenv_description = optarg;
        return ESPANK_SUCCESS;
    }};

static spank_option view_arg{
    (char*)"view",
    (char*)"[uenv:]view-name[,<uenv:view-name>]*",
    (char*)"A comma separated list of uenv views",
    1, // requires an argument
    0, // plugin specific value to pass to the callback (unnused)
    [](int val, const char* optarg, int remote) -> int {
        slurm_verbose("uenv: val:%d optarg:%s remote:%d", val, optarg, remote);
        args.view_description = optarg;
        return ESPANK_SUCCESS;
    }};

static spank_option repo_arg{
    (char*)"repo",
    (char*)"path*",
    (char*)"the absolute path of a uenv repository used to look up uenv images",
    1, // requires an argument
    0, // plugin specific value to pass to the callback (unnused)
    [](int val, const char* optarg, int remote) -> int {
        slurm_verbose("uenv: val:%d optarg:%s remote:%d", val, optarg, remote);
        args.repo_description = optarg;
        return ESPANK_SUCCESS;
    }};

int slurm_spank_init(spank_t sp, int ac [[maybe_unused]],
                     char** av [[maybe_unused]]) {

    for (auto arg : {&uenv_arg, &view_arg, &repo_arg}) {
        if (auto status = spank_option_register(sp, arg)) {
            return status;
        }
    }

    return ESPANK_SUCCESS;
}

/// check if image, mountpoint is valid
int init_post_opt_remote(spank_t sp) {
    // initialise logging
    // level warning to console
    // level info to syslog
    uenv::init_log(spdlog::level::warn, spdlog::level::info);

    // parse environment variables to test whether there is anything to mount
    auto mount_var = getenv_wrapper(sp, "UENV_MOUNT_LIST");

    // variable is not set - nothing to do here
    if (!mount_var) {
        return ESPANK_SUCCESS;
    }

    auto mount_list = validate_uenv_mount_list(*mount_var);
    if (!mount_list) {
        slurm_error("internal error parsing the mount list: %s",
                    mount_list.error().c_str());
        return -ESPANK_ERROR;
    }

    auto result = do_mount(*mount_list);
    if (!result) {
        slurm_error("error mounting the requested uenv image: %s",
                    result.error().c_str());
        return -ESPANK_ERROR;
    }

    return ESPANK_SUCCESS;
}

/// parse and validate the CLI arguments
/// set environment variables that are used in the remote context to mount the
/// image set environment variables for all requested views
int init_post_opt_local_allocator(spank_t sp [[maybe_unused]]) {
    // grab a snapshot of the calling environment
    // this function is called in the local context (where srun and sbatch are
    // called), where the standard setenv/getenv interface is used to access
    // environment variables.
    const auto calling_environment = envvars::state(environ);

    // initialise logging
    uenv::set_log_level(calling_environment);

    if (!args.uenv_description) {
        // it is an error if the view argument was passed without the uenv
        // argument
        if (args.view_description) {
            slurm_error("the uenv --view=%s argument is set, but the --uenv "
                        "argument was not",
                        args.view_description->c_str());
            return -ESPANK_ERROR;
        }

        // check whether a uenv has been mounted in the calling environment.
        // this will be mounted in the remote context, so check that:
        // * the squashfs image exists
        // * the user has read access to the squashfs image
        // * the mount point exists
        if (auto mount_var = calling_environment.get("UENV_MOUNT_LIST")) {
            if (auto mount_list = validate_uenv_mount_list(*mount_var);
                !mount_list) {
                slurm_error("invalid UENV_MOUNT_LIST: %s",
                            mount_list.error().c_str());
                return -ESPANK_ERROR;
            }
        }

        return ESPANK_SUCCESS;
    }

    // if no repository was explicitly set using the --repo argument, check
    // UENV_REPO_PATH environment variable, before using default in SCRATCH or
    // HOME.
    if (!args.repo_description) {
        args.repo_description = uenv::default_repo_path(calling_environment);
        if (!args.repo_description) {
            slurm_error("unable to find a valid repo path");
            return -ESPANK_ERROR;
        }
    }

    // parse and validate the repo path if one is set
    // - it is a valid path description
    // - it is an absolute path
    // - it exists
    const auto r =
        uenv::validate_repo_path(*args.repo_description, false, false);
    if (!r) {
        slurm_error("unable to find a valid repo path: %s", r.error().c_str());
        return -ESPANK_ERROR;
    }
    std::optional<std::filesystem::path> repo_path = *r;

    const auto env =
        uenv::concretise_env(*args.uenv_description, args.view_description,
                             repo_path, calling_environment);

    if (!env) {
        slurm_error("%s", env.error().c_str());
        return -ESPANK_ERROR;
    }

    // set the environment variables
    auto runtime_environment =
        generate_slurm_environment(*env, calling_environment);

    // set additional environment variables that are required to communicate
    // with the remote plugin.
    std::unordered_map<std::string, std::string> uenv_vars;

    std::vector<std::string> uenv_mount_list;
    for (auto& e : env->uenvs) {
        auto& u = e.second;
        uenv_mount_list.push_back(
            fmt::format("{}:{}", u.sqfs_path.string(), u.mount_path.string()));
    }
    runtime_environment.set("UENV_MOUNT_LIST",
                            fmt::format("{}", fmt::join(uenv_mount_list, ",")));

    for (auto& var : runtime_environment.variables()) {
        ::setenv(var.first.c_str(), var.second.c_str(), 1);
    }

    return ESPANK_SUCCESS;
}

int slurm_spank_init_post_opt(spank_t sp, int ac [[maybe_unused]],
                              char** av [[maybe_unused]]) {
    switch (spank_context()) {
    case spank_context_t::S_CTX_REMOTE: {
        return init_post_opt_remote(sp);
    }
    case spank_context_t::S_CTX_LOCAL:
    case spank_context_t::S_CTX_ALLOCATOR: {
        return init_post_opt_local_allocator(sp);
    }
    default:
        break;
    }

    return ESPANK_SUCCESS;
}

} // namespace impl
