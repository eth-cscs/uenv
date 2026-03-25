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

#include <site/site.h>
#include <uenv/env.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/color.h>
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

namespace impl {

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

// return the full uenv_info description of a record from an on-disk repository
//
// resolves the squashfs path, meta data and record
util::expected<uenv_info, std::string>
resolve_uenv_info(const uenv_record& record, const repository& store) {
    namespace fs = std::filesystem;

    if (store.is_in_memory()) {
        return util::unexpected{
            "(internal error) the store is not from an on-disk repository"};
    }

    uenv_info info;
    info.record = record;

    // set sqfs_path and digest
    info.sqfs_path = fs::absolute(store.uenv_paths(record.sha).squashfs);
    if (!fs::exists(info.sqfs_path) || !fs::is_regular_file(info.sqfs_path)) {
        return unexpected(fmt::format(
            "the uenv image {} does not exist or is not a file. Run "
            "'uenv repo status' to check the health of the repo.",
            info.sqfs_path));
    }
    spdlog::info("{} squashfs image {}", record, info.sqfs_path.string());

    // if meta/env.json exists, parse the json therein
    auto meta = find_meta_path(info.sqfs_path);
    info.meta_path = meta.path;

    if (meta.env) {
        if (const auto result = uenv::load_meta(*(meta.env))) {
            info.meta = result.value();
            spdlog::info("loaded meta (name {}, mount {})", info.meta->name,
                         info.meta->mount);
        } else {
            spdlog::warn("skipping the uenv meta data {}: {}",
                         meta.env->string(), result.error());
        }
    } else {
        spdlog::warn("no meta file available for {}", info.sqfs_path.string());
    }

    return info;
}

// resolve uenv_info for a uenv image defined by the path of a squashfs file
util::expected<uenv_info, std::string>
resolve_uenv_info(const std::filesystem::path& sqfs_path) {
    namespace fs = std::filesystem;

    uenv_info info;

    // set sqfs_path and digest
    info.sqfs_path = fs::absolute(sqfs_path);
    if (!fs::exists(info.sqfs_path) || !fs::is_regular_file(info.sqfs_path)) {
        return unexpected(fmt::format(
            "the uenv image {} does not exist or is not a file. Run "
            "'uenv repo status' to check the health of the repo.",
            info.sqfs_path));
    }

    // if meta/env.json exists, parse the json therein
    auto meta = impl::find_meta_path(info.sqfs_path);
    info.meta_path = meta.path;

    if (meta.env) {
        if (const auto result = uenv::load_meta(meta.env.value())) {
            info.meta = result.value();
            spdlog::info("loaded meta (name {}, mount {})", info.meta->name,
                         info.meta->mount);
        } else {
            spdlog::warn("opening the uenv meta data {}: {}",
                         meta.env->string(), result.error());
        }
    } else {
        spdlog::warn("no meta file available for {}", info.sqfs_path.string());
    }

    return info;
}

} // namespace impl

// look for matches for a label in a repo, returning a vector of all matches
util::expected<resolved_record_set, std::string>
search_repo(const uenv_label& label, const repo_description& repo) {
    spdlog::info("search_repo: {} in {}", label, repo);

    auto store = uenv::open_repository(repo.path);
    if (!store) {
        return unexpected(
            fmt::format("unable to open repo: {}", store.error()));
    }

    const auto result = store->query(label);
    if (!result) {
        return unexpected(fmt::format("{}", store.error()));
    }

    std::vector<uenv_info> infos;
    for (auto& r : result.value()) {
        if (auto info = impl::resolve_uenv_info(r, store.value())) {
            infos.push_back(std::move(info.value()));
        } else {
            return util::unexpected{info.error()};
        }
    }

    return resolved_record_set{repo, infos};
}

util::expected<uenv_info, std::string>
resolve_uenv(const uenv_label& label,
             const std::vector<repo_description>& repos) {
    spdlog::debug("resolve_uenv: searching for {}", label);
    for (auto& repo : repos) {
        spdlog::debug("resolve_uenv: search in {}", repo);
        auto result = search_repo(label, repo);
        if (result) {
            if (result->empty()) {
                spdlog::warn("resolve_uenv: no matches");
            } else if (!result->unique_sha()) {
                auto errmsg = fmt::format(
                    "more than one uenv matches the uenv description "
                    "'{}':\n",
                    label);
                errmsg += format_record_set_table(result->as_record_set());
                return unexpected(errmsg);
            }
            // there is a unique match
            return *(result->begin());
        } else {
            spdlog::error("search_repo {}: {}", repo, result.error());
            continue;
        }
    }

    return unexpected(fmt::format(
        "no uenv matches '{}'.\n"
        "See available uenv using {}.\n"
        "Use {} and {} to download images before using them.",
        label, color::yellow("uenv image ls"), color::yellow("uenv image find"),
        color::yellow("uenv image pull")));
}

util::expected<uenv_info, std::string>
resolve_uenv(const uenv_description& desc,
             const std::vector<repo_description>& repos) {
    if (desc.label()) {
        return resolve_uenv(desc.label().value(), repos);
    }
    return impl::resolve_uenv_info(desc.filename().value());
}

util::expected<env, std::string>
concretise_env(const std::string& uenv_args,
               const std::optional<std::string>& view_args,
               const std::vector<repo_description>& repos) {
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
        // Resolve uenv information (squashfs path, metadata, etc.)
        auto info_result = resolve_uenv(desc, repos);
        if (!info_result) {
            return unexpected(info_result.error());
        }
        auto& info = *info_result;

        // Determine the name for this uenv:
        // - use the name from the repo database if the image is in a database
        // - else fall back to the name set in the uenv meta-data
        // - else generate an anonymous name (ensuring uniqueness)
        std::string name;
        if (info.record) {
            name = info.record->name;
            spdlog::debug("concretise_env: setting name {} based on record",
                          name);
        } else if (info.meta) {
            name = info.meta->name;
            spdlog::debug("concretise_env: setting name {} based on metadata",
                          name);
        } else {
            name = "anonymous";
            unsigned name_idx = 0;
            while (uenvs.count(name)) {
                name = fmt::format("anonymous{}", name_idx);
                ++name_idx;
            }
            spdlog::debug("concretise_env: setting name {}", name);
        }

        // handle the case where no mount point was provided by the CLI or
        // meta data
        if (!desc.mount() && !info.meta) {
            return unexpected(
                fmt::format("no mount point provided for {}", desc));
        }

        // if an explicit mount point was provided, use that
        // otherwise use the mount point provided in the meta data
        std::string mount_string =
            desc.mount() ? desc.mount().value() : info.meta->mount;

        // TODO: hand off validation to the mount.cpp implementation:
        // For now leave this as is - it is working and well-tested.
        // - the implementation below won't work when / if we have nested mount
        //   points
        //      - it is possible that modular uenv could use nested mounts
        // - NOTE: maybe add the is_absolute test to mount.cpp
        // - NOTE: add the unique mount point test to mount.cpp

        fs::path mount;
        if (auto p = parse_path(mount_string)) {
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

            auto canonical_sqfs = fs::canonical(info.sqfs_path);
            if (used_sqfs.count(canonical_sqfs)) {
                return unexpected(fmt::format(
                    "the '{}' uenv is mounted more than once", canonical_sqfs));
            }
            used_sqfs.insert(canonical_sqfs);
        }

        const bool has_meta = (bool)info.meta;
        const bool has_record = (bool)info.record;

        uenvs[name] = concrete_uenv{
            .name = name,
            .label = has_record ? fmt::format("{}", info.record)
                                : decltype(concrete_uenv::label){},
            .digest = has_record ? fmt::format("{}", info.record->sha)
                                 : decltype(concrete_uenv::digest){},
            .mount_path = mount,
            .sqfs_path = info.sqfs_path,
            .meta_path = info.meta_path,
            .description = has_meta ? info.meta->description.value_or("") : "",
            .views =
                has_meta ? info.meta->views : decltype(concrete_uenv::views){}};
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

resolved_record_set::resolved_record_set(repo_description repo,
                                         std::vector<uenv_info> infos)
    : records_(std::move(infos)), repo_(std::move(repo)) {
    for (auto& entry : records_) {
        if (entry.record) {
            throw std::runtime_error(
                "(internal error) attempt to initialise resolved_record_set "
                "with records that do not have records");
        }
    }
}

const repo_description& resolved_record_set::repo() const {
    return repo_;
}
bool resolved_record_set::empty() const {
    return records_.empty();
}
std::size_t resolved_record_set::size() const {
    return records_.size();
}

bool resolved_record_set::unique_sha() const {
    if (empty()) {
        return false;
    }
    if (size() == 1u) {
        return true;
    }
    auto sha = records_.front().record->sha;
    for (auto& r : records_) {
        if (r.record->sha != sha) {
            return false;
        }
    }
    return true;
};

resolved_record_set::iterator resolved_record_set::begin() {
    return records_.begin();
}
resolved_record_set::iterator resolved_record_set::end() {
    return records_.end();
}
resolved_record_set::const_iterator resolved_record_set::begin() const {
    return records_.begin();
}
resolved_record_set::const_iterator resolved_record_set::end() const {
    return records_.end();
}
resolved_record_set::const_iterator resolved_record_set::cbegin() const {
    return records_.cbegin();
}
resolved_record_set::const_iterator resolved_record_set::cend() const {
    return records_.cend();
}

record_set resolved_record_set::as_record_set() const {
    std::vector<uenv_record> out{};
    out.reserve(records_.size());
    for (auto r : records_) {
        out.push_back(r.record.value());
    }
    return out;
}

} // namespace uenv
