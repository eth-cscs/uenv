#include <charconv>
#include <cstdio>
#include <optional>
#include <string>

#include <unistd.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/config.h>
#include <uenv/env.h>
#include <uenv/log.h>
#include <uenv/mount.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/telemetry.h>
#include <util/envvars.h>
#include <util/expected.h>

#include "elastic.h"
#include "environ.h"
#include "plugin.h"

extern "C" {
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
}

//
// Forward declare the implementation of the plugin callbacks.
//

namespace impl {
int slurm_spank_init(spank_t sp, int ac, char** av);
int slurm_spank_init_post_opt(spank_t sp, int ac, char** av);
int slurm_spank_local_user_init(spank_t sp, int ac, char** av);
int slurm_spank_task_init(spank_t sp, int ac, char** av);

// The backend hooks below are defined in the backend source selected at build
// time: plugin_kernel.cpp (kernel squashfs driver) or plugin_fuse.cpp.

// Mounts the squashfs images in the remote context (kernel backend; a no-op in
// the FUSE backend). Returns an error instead of reporting it to Slurm: see
// "Error policy" below.
util::expected<void, std::string> init_post_opt_remote(spank_t sp);

// Called in each task, as the job user, just before the task's command is
// exec'd (FUSE backend: mounts the images; a no-op in the kernel backend).
// It is only called if nothing failed earlier in the remote context.
int task_init(spank_t sp);
} // namespace impl

//
// Error policy
//
// The plugin must never cause Slurm to drain a node.
//
// When a SPANK hook that runs on the compute node (in slurmstepd) returns an
// error, Slurm may treat the node as faulty: a failed batch step drains the
// node ("batch job complete failure" in slurmctld). Nearly every error this
// plugin can meet on the node is caused by the job (a mount list naming an
// image that the user cannot read, a corrupt image), and even the ones that
// might not be are not worth losing a node to.
//
// The rule, hook by hook:
//
// * slurm_spank_init_post_opt, on the submit side (srun, sbatch, salloc):
//   errors and exceptions are returned to Slurm. They abort the user's
//   command, and no node is involved.
// * slurm_spank_init_post_opt, on the compute node (slurmstepd, before the
//   tasks are forked): errors and exceptions are never returned to Slurm.
//   defer_remote_error() logs and records them, and slurm_spank_task_init
//   then fails every task of the step on this node before its command is
//   exec'd, so that the command never runs without the uenv it asked for.
// * slurm_spank_task_init: errors and exceptions fail the task.
// * slurm_spank_local_user_init (srun only): an exception is logged and srun
//   continues, because the telemetry posted there is optional.
// * slurm_spank_init and the option callbacks have no try/catch. They only
//   register options and copy their arguments, so nothing but bad_alloc can
//   come out of them. A failure of spank_option_register is still returned:
//   it can only be caused by the plugin's own option table, so it would fail
//   every srun and sbatch on the submit side too, and could not go unnoticed.
//
// The handling is written out in each hook, deliberately, instead of in one
// helper that chooses by spank_context(): the context is S_CTX_REMOTE both in
// slurmstepd before the tasks are forked and inside each task, where returning
// success would exec the command without its uenv.
//
// A failing slurm_spank_task_init makes Slurm _exit(1) the task before execve
// (exec_task() in src/slurmd/slurmstepd/task.c, unchanged from at least 23.02
// to 25.11). To Slurm that is an ordinary task failure: the job fails and the
// node stays up. The task's stdio is already connected at that point, so the
// error reaches the user's terminal or batch output, whereas an error logged
// from slurm_spank_init_post_opt on the compute node only reaches the slurmd
// log.
//
// Do not replace this with ESPANK_JOB_FAILURE returned from init_post_opt.
// That code exists only from Slurm 25.05, and it is only documented for
// slurm_spank_init, via slurm_spank_init_failure_mode.
//
// A task_init error is ignored if the plugin is configured as optional in
// plugstack.conf, so the plugin must be configured as required (as it already
// had to be for any of its errors to have effect).
//

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

int slurm_spank_local_user_init(spank_t sp, int ac, char** av) {
    return impl::slurm_spank_local_user_init(sp, ac, av);
}

int slurm_spank_init_post_opt(spank_t sp, int ac, char** av) {
    return impl::slurm_spank_init_post_opt(sp, ac, av);
}

int slurm_spank_task_init(spank_t sp, int ac, char** av) {
    return impl::slurm_spank_task_init(sp, ac, av);
}

} // extern "C"

//
// Implementation
//
namespace impl {

static uenv::slurm::arg_pack args{};

// telemetry is intialized with information about the mounted uenv images that
// is to be sent to elastic logs when arguments are parsed in
// init_post_opt_local_allocator. It is stored in this global variable for
// logging in the later call to slurm_spank_local_user_init, when the slurm
// jobid and stepid are known.
static std::vector<uenv::telemetry_data> telemetry_g;

// configuration loaded from file
static uenv::configuration config_g;

// Set when slurm_spank_init_post_opt fails on the compute node, so that
// slurm_spank_task_init fails the tasks: see "Error policy" above. The tasks
// are forked from slurmstepd after that hook, so each of them inherits both.
// The flag is a plain bool so that recording a failure can not itself fail;
// the message is only for reporting, and may be missing if copying it failed.
static bool remote_failed_g = false;
static std::optional<std::string> remote_error_g;

// Record a failure on the compute node, instead of returning it to Slurm.
// It is logged here, where it reaches the slurmd log for the admin: the copy
// that slurm_spank_task_init prints is what the user sees.
void defer_remote_error(const char* msg) noexcept {
    remote_failed_g = true;
    slurm_error("uenv: %s", msg);
    if (!remote_error_g) {
        try {
            remote_error_g = msg;
        } catch (...) {
            // slurm_spank_task_init prints a generic message instead
        }
    }
}

// Print an error from inside a task, where stderr is the task's stderr: the
// user's terminal, or the output file of a batch job. This is used instead of
// slurm_error, because slurmstepd's log is buffered and the task leaves with
// _exit(1), which could lose a buffered line.
void print_task_error(const char* msg) noexcept {
    std::fprintf(stderr, "uenv: error: %s\n", msg);
    std::fflush(stderr);
}

static spank_option uenv_arg{
    (char*)"uenv",
    (char*)"<file>[:mount-point][,<file:mount-point>]*",
    (char*)"A comma seprated list of uenv",
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

static spank_option disable_view_arg{
    (char*)"no-default-view",
    (char*)"",
    (char*)"disable default views: only views explicitly set using the --view "
           "flag will be used",
    0, // does not take an argument
    0, // plugin specific value to pass to the callback (unused)
    [](int val [[maybe_unused]], const char* optarg [[maybe_unused]],
       int remote [[maybe_unused]]) -> int {
        args.use_default_views = false;
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

static spank_option passthrough_arg{
    (char*)"uenv-passthrough",
    (char*)"how to treat uenv that are running when srun/sbatch/salloc is "
           "called",
    (char*)"one of [use, ignore, disable]",
    1, // requires an argument
    0, // plugin specific value to pass to the callback (unnused)
    [](int val [[maybe_unused]], const char* optarg,
       int remote [[maybe_unused]]) -> int {
        const auto arg = std::string_view(optarg);
        using enum uenv::slurm::passthrough_policy;
        if (arg == "use") {
            args.passthrough = use;
        } else if (arg == "ignore") {
            args.passthrough = ignore;
        } else if (arg == "disable") {
            args.passthrough = disable;
        } else {
            return -ESPANK_ERROR;
        }

        return ESPANK_SUCCESS;
    }};

int slurm_spank_init(spank_t sp, int ac [[maybe_unused]],
                     char** av [[maybe_unused]]) {

    for (auto arg : {&uenv_arg, &view_arg, &repo_arg, &disable_view_arg,
                     &passthrough_arg}) {
        if (auto status = spank_option_register(sp, arg)) {
            return status;
        }
    }

    return ESPANK_SUCCESS;
}

//
// helper functions
//

void set_log_level(const envvars::state& env) {
    // disable logging by default
    auto log_level = spdlog::level::off;
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
    uenv::init_log(log_level);
    if (invalid_env) {
        spdlog::warn(fmt::format("UENV_LOG_LEVEL invalid value '{}' -- "
                                 "expected a value between 0 and 3",
                                 log_level_str));
    }
}

// Called in the local context (srun) after resources have been allocated, when
// the job id and step id are known. This information is required to create
// elastic logs that can be correlated with Slurm logs.
//
// The telemetry is optional, so an exception is logged and srun continues.
int slurm_spank_local_user_init(spank_t sp [[maybe_unused]],
                                int ac [[maybe_unused]],
                                char** av [[maybe_unused]]) {
    try {
        const bool log_in_subprocess = true;

        const auto calling_environment = envvars::state(environ);
        set_log_level(calling_environment);

        // only log to elastic if both telemetry data and the elastic target
        // were set
        if (!telemetry_g.empty() && config_g.elastic_config) {
            if (auto payload = uenv::slurm_elastic_payload(
                    telemetry_g, calling_environment)) {
                uenv::post_elastic(payload.value(),
                                   config_g.elastic_config.value(),
                                   log_in_subprocess);
            }
        }
    } catch (const std::exception& e) {
        slurm_error("uenv: unable to post telemetry: %s", e.what());
    } catch (...) {
        slurm_error("uenv: unable to post telemetry: unknown exception");
    }

    return ESPANK_SUCCESS;
}

// Called in the local and allocator contexts in slurm_spank_init_post_opt.
//
// Parses command line arguments and validates that they describe a correct and
// consistent environment, then sets environment variables and encodes the
// squashfs images and their mount points in the UENV_MOUNT_LIST environment
// variable which will be read in the remote context, where the images are
// mounted.
//
// * parse and validate the CLI arguments
// * set environment variables that are used in the remote context to mount
//   the image
// * set environment variables for all requested views
int init_post_opt_local_allocator(spank_t sp [[maybe_unused]]) {
    // grab a snapshot of the calling environment
    // this function is called in the local context (where srun and sbatch
    // are called), where the standard setenv/getenv interface is used to
    // access environment variables.
    const auto calling_environment = envvars::state(environ);

    // initialise logging
    set_log_level(calling_environment);

    // log the version of uenv being used
    spdlog::info("uenv version {}", UENV_VERSION);

    // check whether SBATCH_UENV or SBATCH_UENV_VIEW has been set
    // the arguments passed via --uenv and --view take precedence
    args.uenv_description = args.uenv_description
                                ? args.uenv_description
                                : calling_environment.get("SBATCH_UENV");
    args.view_description = args.view_description
                                ? args.view_description
                                : calling_environment.get("SBATCH_UENV_VIEW");
    args.repo_description = args.repo_description
                                ? args.repo_description
                                : calling_environment.get("SBATCH_UENV_REPO");

    const bool in_sbatch = spank_context() == spank_context_t::S_CTX_ALLOCATOR;
    const bool in_srun = spank_context() == spank_context_t::S_CTX_LOCAL;

    // set the default treatment of uenv in the calling environment.
    // srun: forward the environment to compute nodes
    // sbatch: treat as a hard error (force user to declare what they intend)
    using enum uenv::slurm::passthrough_policy;
    if (args.passthrough == none) {
        args.passthrough = in_srun ? use : disable;
    }

    const bool uenv_is_loaded = uenv::in_uenv_session(calling_environment);

    // Slurm replays allocation-level SPANK options (e.g. --uenv=tool set via
    // #SBATCH) for every srun step. Inside a running Slurm uenv job this would
    // set args.uenv_description to the allocation's value, causing the plugin
    // to re-apply view patches on every step instead of entering passthrough
    // mode. Detect the replay by comparing to SLURM_UENV: if they match the
    // user did not override the uenv on this srun invocation, so clear the args
    // to get passthrough behaviour. A mismatch means a genuine explicit
    // override (e.g. srun --uenv=other inside a job allocated with
    // --uenv=tool).
    if (in_srun && uenv::slurm::in_slurm_uenv_session(calling_environment)) {
        const auto slurm_uenv = calling_environment.get("SLURM_UENV");
        if (slurm_uenv && args.uenv_description &&
            *args.uenv_description == *slurm_uenv) {
            args.uenv_description = std::nullopt;
            args.view_description = std::nullopt;
            args.repo_description = std::nullopt;
        }
    }

    const bool has_uenv_args = (bool)args.uenv_description;

    // clear any SBATCH env variables: these should not propogate to nested
    // srun/sbatch calls.
    ::unsetenv("SBATCH_UENV");
    ::unsetenv("SBATCH_UENV_VIEW");
    ::unsetenv("SBATCH_UENV_REPO");

    // it is an error if uenv arguments are passed without the --uenv
    // argument also set
    if (!has_uenv_args && (args.view_description || args.repo_description)) {
        slurm_error("the uenv --view and --repo argument can not be set "
                    "when the --uenv argument is not provided");
        return -ESPANK_ERROR;
    }

    // nothing to do here: no uenv is requested and no uenv is present in the
    // calling environment (or the user has explicitly requested we ignore uenv
    // in the calling environmnet).
    if (!has_uenv_args && (args.passthrough == ignore || !uenv_is_loaded)) {
        // remove all traces of uenv from the environment
        ::unsetenv("UENV_MOUNT_LIST");
        ::unsetenv("UENV_VIEW");
        ::unsetenv("UENV_REPO");
        ::unsetenv("UENV_TELEMETRY");
        ::unsetenv("SLURM_UENV");
        ::unsetenv("SLURM_UENV_VIEW");
        ::unsetenv("SLURM_UENV_REPO");
        return ESPANK_SUCCESS;
    }

    // if host environment pass through has been disabled check whether
    // a uenv is already loaded AND no uenv has been requested
    if (args.passthrough == disable && uenv_is_loaded && !has_uenv_args) {
        if (in_sbatch) {
            slurm_error(
                "Calling sbatch/salloc from inside a uenv session is "
                "disabled by default.\n"
                "Set --uenv-passthrough=use to use the uenv inside the "
                "job, or --uenv-passthrough=ignore to ignore loaded uenv.\n"
                "Note that it is discouraged to call sbatch with a uenv "
                "loaded: use --uenv directly to make your intentions clear.");
        }
        if (in_srun) {
            slurm_error(
                "Calling srun from inside a uenv session has been disabled.\n"
                "Set --uenv-passthrough=use to use the uenv inside the "
                "job, or --uenv-passthrough=ignore to ignore loaded uenv.\n"
                "Note that the default behavior of srun is to mount, which can "
                "be restored by removing the --uenv-passthrough argument.");
        }
        return -ESPANK_ERROR;
    }

    // if calling srun without any uenv arguments and a uenv loaded
    const bool pass_through =
        args.passthrough == use && !has_uenv_args && uenv_is_loaded;
    if (pass_through) {
        // verify that the mounts provided by UENV_MOUNT_LIST are valid
        const auto mount_var = calling_environment.get("UENV_MOUNT_LIST");
        if (auto mount_list =
                uenv::parse_and_validate_mounts(mount_var.value());
            !mount_list) {
            slurm_error("invalid UENV_MOUNT_LIST: %s",
                        mount_list.error().c_str());
            return -ESPANK_ERROR;
        }

        // derive telemetry information from the calling environment
        if (auto result = uenv::telemetry_from_env(calling_environment)) {
            telemetry_g = result.value();
        }

        // load elastic config so telemetry can be posted (the new-mount path
        // does this too; passthrough must do it here since it skips that
        // branch)
        if (auto full_config = uenv::load_config({}, {}, calling_environment)) {
            config_g = uenv::generate_configuration(full_config.value());
        }

        // srun called from login node with a uenv loaded
        //      - UENV_*  set
        //      - SLURM_* not set
        // try to set SLURM_UENV variables as best we can
        if (!uenv::slurm::in_slurm_uenv_session(calling_environment)) {
            const auto view_var = calling_environment.get("UENV_VIEW");
            const auto repo_var = calling_environment.get("UENV_REPO");
            const auto uenv_var = calling_environment.get("UENV_LABEL");
            std::string view_forwarded;
            if (view_var) {
                if (const auto r =
                        uenv::parse_env_view_description(view_var.value())) {
                    view_forwarded = view_var.value();
                } else {
                    // do not treat this as a hard error, but log it clearly.
                    slurm_error("UENV_VIEW is malformed, ignoring: %s",
                                r.error().message().c_str());
                }
            }
            ::setenv("SLURM_UENV", uenv_var.value_or("").c_str(), 1);
            ::setenv("SLURM_UENV_VIEW", view_forwarded.c_str(), 1);
            ::setenv("SLURM_UENV_REPO", repo_var.value_or("").c_str(), 1);
        }
    }
    // a new uenv has to be mounted
    else {
        // assert that a uenv has been explicitly requested
        if (!has_uenv_args) {
            slurm_error(
                "fatal error: unable to mount uenv because none requested. "
                "File a ticket at https://support.cscs.ch");
            return -ESPANK_ERROR;
        }
        // parse the repo flag if it was passed
        std::optional<std::vector<uenv::repo_label>> cli_repo_labels{};
        if (args.repo_description) {
            if (const auto result =
                    uenv::parse_repo_list(args.repo_description.value())) {
                cli_repo_labels = result.value();
            } else {
                slurm_error("invalid --repo argument: %s",
                            result.error().description.c_str());
                return -ESPANK_ERROR;
            }
        }

        if (auto full_config =
                uenv::load_config({}, cli_repo_labels, calling_environment)) {
            config_g = uenv::generate_configuration(full_config.value());
        } else {
            slurm_error("%s", full_config.error().c_str());
            return -ESPANK_ERROR;
        }

        const auto resolved =
            uenv::resolve_uenv_args(args.uenv_description.value(),
                                    config_g.repos, config_g.system_name);
        if (!resolved) {
            slurm_error("%s", resolved.error().c_str());
            return -ESPANK_ERROR;
        }

        const auto env =
            uenv::concretise_env(resolved.value(), args.view_description,
                                 args.uenv_description.value(), config_g.repos,
                                 args.use_default_views);

        if (!env) {
            slurm_error("%s", env.error().c_str());
            return -ESPANK_ERROR;
        }

        // patch the environment variables in the calling environment: calls
        // the setenv and unsetenv to adjust the variables in the calling
        // environment.
        // Also sets SLURM_UENV* variables.
        patch_slurm_environment(*env, calling_environment, args);

        telemetry_g = uenv::make_telemetry(*env);
    }

    return ESPANK_SUCCESS;
}

int slurm_spank_init_post_opt(spank_t sp, int ac [[maybe_unused]],
                              char** av [[maybe_unused]]) {
    switch (spank_context()) {
    // On the compute node: never return an error, defer it to
    // slurm_spank_task_init instead (see "Error policy" above).
    case spank_context_t::S_CTX_REMOTE: {
        try {
            if (auto result = init_post_opt_remote(sp); !result) {
                defer_remote_error(result.error().c_str());
            }
        } catch (const std::exception& e) {
            defer_remote_error(e.what());
        } catch (...) {
            defer_remote_error("unknown exception while mounting the uenv");
        }
        return ESPANK_SUCCESS;
    }
    // On the submit side: an error fails the srun, sbatch or salloc command.
    case spank_context_t::S_CTX_LOCAL:
    case spank_context_t::S_CTX_ALLOCATOR: {
        try {
            return init_post_opt_local_allocator(sp);
        } catch (const std::exception& e) {
            slurm_error("uenv: %s", e.what());
        } catch (...) {
            slurm_error("uenv: unknown exception");
        }
        return -ESPANK_ERROR;
    }
    default:
        break;
    }

    return ESPANK_SUCCESS;
}

// Called in each task on the compute node, as the job user, just before the
// task's command is exec'd. Any error here, and any exception, fails the task:
// Slurm then _exit(1)s it, which fails the job but not the node.
int slurm_spank_task_init(spank_t sp, int ac [[maybe_unused]],
                          char** av [[maybe_unused]]) {
    if (remote_failed_g) {
        // every task of the step on this node fails with the same error:
        // print it once per node, from local task 0
        int task_id = 0;
        if (spank_get_item(sp, S_TASK_ID, &task_id) != ESPANK_SUCCESS ||
            task_id == 0) {
            print_task_error(remote_error_g
                                 ? remote_error_g->c_str()
                                 : "unable to mount the uenv (the reason is "
                                   "in the slurmd log of this node)");
        }
        return -ESPANK_ERROR;
    }

    try {
        return task_init(sp);
    } catch (const std::exception& e) {
        print_task_error(e.what());
    } catch (...) {
        print_task_error("unknown exception while mounting the uenv");
    }
    return -ESPANK_ERROR;
}

} // namespace impl
