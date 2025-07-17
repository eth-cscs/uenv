#include <stdlib.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

#include <site/site.h>
#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/envvars.h>
#include <util/fs.h>
#include <util/subprocess.h>

namespace uenv {

using util::unexpected;

// a convenience helper that merges all of the environment variable patches, one
// for each view, into a single patch
envvars::patch env::patch() const {
    if (views.empty()) {
        return {};
    }
    if (views.size() == 1u) {
        return uenvs.at(views[0].uenv).views.at(views[0].name).environment;
    }

    envvars::patch p{};

    for (auto& view : views) {
        // Environment.views is a list of {uenv.name, view.name} pairs.
        // to get the concrete view, look up the view in the appropriate uenv
        // definition.
        // Performed in this awkward way to ensure that views are applied in the
        // correct order (which matches the order that the user specified them
        // in the --views command)
        p.merge(uenvs.at(view.uenv).views.at(view.name).environment);
    }

    return p;
}

// returns true iff in a running uenv session
bool in_uenv_session(const envvars::state& e) {
    return e.get("UENV_MOUNT_LIST") && e.get("UENV_VIEW");
}

struct meta_info {
    std::optional<std::filesystem::path> path;
    std::optional<std::filesystem::path> env;
};

meta_info find_meta_path(const std::filesystem::path& sqfs_path) {
    namespace fs = std::filesystem;

    // this test checks whether the meta path contains env.json.
    // extend in the future to check for other required files, as needed.
    auto is_valid_meta_path = [](const std::filesystem::path& path) -> bool {
        return fs::is_regular_file(path / "env.json");
    };

    meta_info meta;

    if (const auto p = sqfs_path.parent_path() / "meta";
        is_valid_meta_path(p)) {
        meta.path = p;
    } else if (const auto p = util::unsquashfs_tmp(sqfs_path, "meta");
               p && is_valid_meta_path(*p / "meta")) {
        meta.path = *p / "meta";
    }

    if (meta.path) {
        spdlog::debug("find_meta_path: {} found meta path {}",
                      sqfs_path.string(), meta.path->string());
    }

    if (meta.path) {
        auto env_meta = meta.path.value() / "env.json";

        if (fs::is_regular_file(env_meta)) {
            meta.env = env_meta;
        }
        if (meta.env) {
            spdlog::debug("find_meta_path: {} found env meta {}",
                          sqfs_path.string(), meta.env->string());
        }
    }

    return meta;
}

util::expected<env, std::string>
concretise_env(const std::string& uenv_args,
               std::optional<std::string> view_args,
               std::optional<std::filesystem::path> repo_arg,
               const envvars::state& calling_env) {
    namespace fs = std::filesystem;

    // parse the uenv description that was provided as a command line
    // argument. the command line argument is a comma-separated list of
    // uenvs, where each uenv is either
    // - the path of a squashfs image; or
    // - a uenv description of the form
    // name[/version][:tag][@system][%uarch] with an optional mount point.
    const auto uenv_descriptions = uenv::parse_uenv_args(uenv_args);
    if (!uenv_descriptions) {
        return unexpected(fmt::format("invalid uenv description: {}",
                                      uenv_descriptions.error().message()));
    }

    // concretise the uenv descriptions by looking for the squashfs file, or
    // looking up the uenv descrition in a registry.
    // after this loop, we have fully validated list of uenvs, mount points
    // and meta data (if they have meta data).

    std::unordered_map<std::string, uenv::concrete_uenv> uenvs;
    std::set<fs::path> used_mounts;
    std::set<fs::path> used_sqfs;
    for (auto& desc : *uenv_descriptions) {
        // determine the sqfs_path
        fs::path sqfs_path;

        // if a label was used to describe the uenv (e.g. "prgenv-gnu/24.7"
        // it has to be looked up in a repo.
        if (auto label = desc.label()) {
            if (!repo_arg) {
                return unexpected("a repo needs to be provided either "
                                  "using the --repo flag "
                                  "or by setting the UENV_REPO_PATH "
                                  "environment variable");
            }
            auto store = uenv::open_repository(*repo_arg);
            if (!store) {
                return unexpected(
                    fmt::format("unable to open repo: {}", store.error()));
            }

            // set label->system to the current cluster name if it has not
            // already been set.
            label->system = site::get_system_name(label->system, calling_env);

            // search for label in the repo
            const auto result = store->query(*label);
            if (!result) {
                return unexpected(fmt::format("{}", store.error()));
            }
            const auto results = *result;

            if (results.empty()) {
                //parse configuration.toml
                toml::table config;
                char *config_location = getenv("UENV_CONFIGURATION_PATH"); //gets config file from UENV_CONFIGURATION_PATH env variable
                std::string_view config_sv{config_location};
                try{
                    config = toml::parse_file(config_sv);
                } catch(const toml::parse_error& err){
                    return unexpected(
                        fmt::format("Error parsing configuration.toml:\n{}",err));
                }

                toml::array& config_repo = *config["uenv_local_repos"].as_array(); //parses toml key as an array of repos

                auto local_results = results;
                //recuse through uenv_local_repo array
                for (auto& repo : config_repo){
                    if (repo.is_string()){ //if repo is a string, then query
                        std::string repo_path_string = repo.value_or("");

                        //validate local repo and skip if validation fails
                        if (auto rpath =
                                uenv::validate_repo_path(repo_path_string, false, false)) {
                            config.repo = path.value();
                        } else {
                            spdlog::warn("invalid repo path {}", rpath.error());
                            break;
                        }

                        //open local repo if valid
                        std::filesystem::path repo_path = repo_path_string;
                        auto local_store = uenv::open_repository(repo_path);

                        const auto local_result = local_store->query(*label);
                        local_results = *local_result;
                        if (!local_results.empty()){ //if repo finds uenv, then set sqfs_path and break
                            const auto& r = *local_results.begin();
                            sqfs_path = local_store->uenv_paths(r.sha).squashfs;
                            //printf("Uenv found in local repository: %s\n", repo.value_or(""));
                            break;
                        }
                    }
                    else{
                        return unexpected(
                            fmt::format("Error: uenv local config isn't a string"));
                    }
                }
                if (local_results.empty()){ //if uenv couldn't be found in local or local repo
                    return unexpected(fmt::format("no uenv matches '{}'", *label));
                }
                // ensure that all results share a unique sha
                if (!local_results.unique_sha()) {
                auto errmsg = fmt::format(
                    "more than one uenv matches the uenv description "
                    "'{}':\n",
                    desc.label().value());
                errmsg += format_record_set(local_results);
                return unexpected(errmsg);
                }
            } else {
                // ensure that all results share a unique sha
                if (!results.unique_sha()) {
                    auto errmsg = fmt::format(
                        "more than one uenv matches the uenv description "
                        "'{}':\n",
                        desc.label().value());
                    errmsg += format_record_set(results);
                    return unexpected(errmsg);
                }

                // set sqfs_path
                const auto& r = *results.begin();
                sqfs_path = store->uenv_paths(r.sha).squashfs;
            }
        }
        // otherwise an explicit filename was provided, e.g.
        // "/scratch/myimages/develp/store.squashfs"
        else {
            sqfs_path = fs::path(*desc.filename());
        }

        sqfs_path = fs::absolute(sqfs_path);
        if (!fs::exists(sqfs_path) || !fs::is_regular_file(sqfs_path)) {
            return unexpected(
                fmt::format("the uenv image {} does not exist or is not a file",
                            sqfs_path));
        }
        spdlog::info("{} squashfs image {}", desc, sqfs_path.string());

        // create a default "anonymous" name for the uenv that will be
        // overwritten if meta data is provided.
        std::string name = "anonymous";
        unsigned name_idx = 0;
        while (uenvs.count(name)) {
            name = fmt::format("anonymous{}", name_idx);
            ++name_idx;
        }
        std::string description = "";
        std::optional<std::string> mount_meta;
        std::unordered_map<std::string, concrete_view> views;

        // if meta/env.json exists, parse the json therein
        auto meta = find_meta_path(sqfs_path);
        if (meta.env) {
            if (const auto result = uenv::load_meta(*(meta.env))) {
                name = std::move(result->name);
                description = result->description.value_or("");
                mount_meta = result->mount;
                views = std::move(result->views);
                spdlog::info("{}: loaded meta (name {}, mount {})", desc, name,
                             mount_meta);
            } else {
                spdlog::warn("{} opening the uenv meta data {}: {}", desc,
                             meta.env->string(), result.error());
            }
        } else {
            spdlog::warn("{} no meta file available for {}", desc,
                         sqfs_path.string());
        }

        // if an explicit mount point was provided, use that
        // otherwise use the mount point provided in the meta data
        auto mount_string = desc.mount() ? desc.mount() : mount_meta;

        // handle the case where no mount point was provided by the CLI or
        // meta data
        if (!mount_string) {
            return unexpected(
                fmt::format("no mount point provided for {}", desc));
        }

        fs::path mount;
        if (auto p = parse_path(*mount_string)) {
            mount = *p;
            if (!fs::exists(mount)) {
                return unexpected(fmt::format(
                    "the mount point {} for {} does not exist", desc, mount));
            }
            if (!fs::is_directory(mount)) {
                return unexpected(
                    fmt::format("the mount point {} for {} is not a directory",
                                desc, mount));
            }
            if (!mount.is_absolute()) {
                return unexpected(fmt::format(
                    "the mount point {} for {} must be an absolute path", desc,
                    mount));
            }
        } else {
            return unexpected(
                fmt::format("invalid mount point provided for {}: {}", desc,
                            p.error().message()));
        }
        spdlog::info("{} will be mounted at {}", desc, mount);

        // check for unique mount points and squashfs images
        {
            mount = fs::canonical(mount);
            if (used_mounts.count(mount)) {
                return unexpected(fmt::format("more than one image mounted "
                                              "at the mount point '{}'",
                                              mount));
            }
            used_mounts.insert(mount);

            sqfs_path = fs::canonical(sqfs_path);
            if (used_sqfs.count(sqfs_path)) {
                return unexpected(fmt::format(
                    "the '{}' uenv is mounted more than once", sqfs_path));
            }
            used_sqfs.insert(sqfs_path);
        }

        uenvs[name] = concrete_uenv{name,      mount,       sqfs_path,
                                    meta.path, description, std::move(views)};
    }

    // A dictionary with view name as a key, and a list of uenv that provide
    // view with that name as the values
    // std::unordered_map<std::string, std::vector<std::string>> view2uenv;
    std::unordered_map<std::string, std::vector<std::string>> view2uenv;
    for (const auto& u : uenvs) {
        for (const auto& v : u.second.views) {
            if (!view2uenv.count(v.first)) {
                view2uenv[v.first] = {u.first};
            } else {
                view2uenv[v.first].push_back(u.first);
            }
        }
    }

    // step 2: parse the views
    //  - parse the CLI view description
    //  - look up view descriptions in the uenv meta data
    std::vector<qualified_view_description> views;
    if (view_args) {
        const auto view_descriptions = uenv::parse_view_args(*view_args);
        if (!view_descriptions) {
            return unexpected(fmt::format("invalid view description: {}",
                                          view_descriptions.error().message()));
        }

        for (auto& view : *view_descriptions) {
            spdlog::debug("analysing view {}", view);

            // check whether the view name matches the name of any views
            // provided by uenv
            if (view2uenv.count(view.name)) {
                // a list of uenv that have a view with name v.name
                const auto& matching_uenvs = view2uenv[view.name];

                // handle the case where no uenv name was provided, e.g.
                // develop
                if (!view.uenv) {
                    // it is ambiguous if more than one option is available
                    if (matching_uenvs.size() > 1) {
                        auto errstr = fmt::format(
                            "there is more than one view named '{}':",
                            view.name);
                        for (auto m : matching_uenvs) {
                            errstr += fmt::format("\n  {}:{}", m, view.name);
                        }
                        return unexpected(errstr);
                    }
                    views.push_back({matching_uenvs[0], view.name});
                }
                // handle the case where both uenv and view name are
                // provided, e.g. prgenv-gnu:develop
                else {
                    auto it = std::find_if(
                        matching_uenvs.begin(), matching_uenvs.end(),
                        [&n = *view.uenv](const std::string& uenv_name) {
                            return uenv_name == n;
                        });
                    // no uenv matches
                    if (it == matching_uenvs.end()) {
                        return unexpected(
                            fmt::format("the view '{}:{}' does not exist",
                                        *view.uenv, view.name));
                    }
                    views.push_back({*it, view.name});
                }
            }
            // no view that matches the view is available
            else {
                return unexpected(
                    fmt::format("the view '{}' does not exist", view.name));
            }
        }
    }

    return env{uenvs, views};
}

// list of environment variables that are ignored in setuid applications
// the full list is defined here:
//      https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/generic/unsecvars.h
std::set<std::string_view> unsecure_envvars__{
    "GCONV_PATH",
    "GETCONF_DIR",
    "GLIBC_TUNABLES",
    "HOSTALIASES",
    "LD_AUDIT",
    "LD_BIND_NOT",
    "LD_BIND_NOW",
    "LD_DEBUG",
    "LD_DEBUG_OUTPUT",
    "LD_DYNAMIC_WEAK",
    "LD_LIBRARY_PATH",
    "LD_ORIGIN_PATH",
    "LD_PRELOAD",
    "LD_PROFILE",
    "LD_SHOW_AUXV",
    "LD_VERBOSE",
    "LD_WARN",
    "LOCALDOMAIN",
    "LOCPATH",
    "MALLOC_ARENA_MAX",
    "MALLOC_ARENA_TEST",
    "MALLOC_MMAP_MAX_",
    "MALLOC_MMAP_THRESHOLD_",
    "MALLOC_PERTURB_",
    "MALLOC_TOP_PAD_",
    "MALLOC_TRACE",
    "MALLOC_TRIM_THRESHOLD_",
    "NIS_PATH",
    "NLSPATH",
    "RESOLV_HOST_CONF",
    "RES_OPTIONS",
    "TMPDIR",
};

envvars::state generate_environment(const env& environment,
                                    envvars::state const& base,
                                    std::optional<std::string> secure_prefix) {
    auto vars = base;

    vars.apply_patch(environment.patch(), envvars::expand_delim::view);

    // set UENV_VIEW env variable, used inside the environment by uenv
    auto view_description = [&environment](const auto& v) {
        return fmt::format("{}:{}:{}", environment.uenvs.at(v.uenv).mount_path,
                           v.uenv, v.name);
    };

    vars.set(
        "UENV_VIEW",
        fmt::format("{}", fmt::join(environment.views |
                                        std::views::transform(view_description),
                                    ",")));

    // search the environment for variables that are security sensitive, and
    // generate renamed copies.
    // Performed in two stages, because directly updating in the first loop
    // would invalidate iterators.
    std::vector<envvars::scalar> secure_vars;
    if (secure_prefix) {
        for (const auto& e : vars.variables()) {
            if (unsecure_envvars__.contains(e.first)) {
                secure_vars.push_back(
                    {secure_prefix.value() + e.first, e.second});
            }
        }
    }
    for (const auto& var : secure_vars) {
        vars.set(var.name, *(var.value));
    }
    return vars;
}

// Update the calling environment to apply the environment variable updates.
// note: making this into a nice clean function that returns state that can be
// used by the calling slurm plugin was too hard - because slurm requires that
// the use of setenv/getenv/unsetenv or spank_getenv/spank_setenv/spank_unsetenv
void patch_slurm_environment(const env& environment,
                             envvars::state const& base) {
    // create the full environment with all patches applied
    envvars::state full_env =
        generate_environment(environment, base, std::nullopt);

    // get the patch that was applied
    auto patch = environment.patch();

    // iterate over all variables in the patch:
    // - if the variable was unset: unset it in the calling environment
    // - if the variable was set: apply the new value to the calling environment
    for (auto& [name, _] : patch.scalars()) {
        if (auto value = full_env.get(name)) {
            setenv(name.c_str(), value->c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }
    for (auto& [name, var] : patch.prefix_paths()) {
        if (auto value = full_env.get(name)) {
            setenv(name.c_str(), value->c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }

    if (auto value = full_env.get("UENV_VIEW")) {
        setenv("UENV_VIEW", value->c_str(), 1);
    }
}

bool operator==(const uenv_record& lhs, const uenv_record& rhs) {
    return std::tie(lhs.name, lhs.version, lhs.tag, lhs.date, lhs.system,
                    lhs.uarch, lhs.sha) ==
           std::tie(rhs.name, rhs.version, rhs.tag, rhs.date, rhs.system,
                    rhs.uarch, rhs.sha);
}

bool operator<(const uenv_record& lhs, const uenv_record& rhs) {
    return std::tie(lhs.name, lhs.version, lhs.tag, lhs.system, lhs.uarch,
                    lhs.date, lhs.sha) < std::tie(rhs.name, rhs.version,
                                                  rhs.tag, rhs.system,
                                                  rhs.uarch, rhs.date, rhs.sha);
}

} // namespace uenv
