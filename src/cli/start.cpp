// vim: ts=4 sts=4 sw=4 et

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/std.h>

#include <uenv/env.h>
#include <uenv/parse.h>
#include <util/expected.h>

#include "start.h"
#include "uenv.h"

namespace uenv {

void start_help() {
    fmt::println("start help");
}

void start_args::add_cli(CLI::App& cli, global_settings& settings) {
    auto* start_cli = cli.add_subcommand("start", "start a uenv session");
    start_cli->add_option("-v,--view", view_description,
                          "comma separated list of views to load");
    start_cli
        ->add_option("uenv", uenv_description,
                     "comma separated list of uenv to mount")
        ->required();
    start_cli->callback([&settings]() { settings.mode = uenv::mode_start; });
}

// stores the parsed and validated settings, based on the CLI arguments.
struct start_settings {
    // these are just descriptions - need to be replaced with concretised types
    std::vector<uenv::uenv_description> uenvs;
    std::vector<uenv::view_description> views;
};

util::expected<const start_settings, std::string>
parse_start_args(const start_args& args) {

    namespace fs = std::filesystem;
    // parse the uenv description that was provided as a command line argument.
    // the command line argument is a comma-separated list of uenvs, where each
    // uenc is either
    // - the path of a squashfs image; or
    // - a uenv description of the form name/version:tag
    // with an optional mount point.
    const auto uenv_descriptions = uenv::parse_uenv_args(args.uenv_description);
    if (!uenv_descriptions) {
        return util::unexpected(
            fmt::format("unable to read the uenv argument\n  {}",
                        uenv_descriptions.error().msg));
    }

    // concretise the uenv descriptions by looking for the squashfs file, or
    // looking up the uenv descrition in a registry.
    // after this loop, we have fully validated list of uenvs, mount points and
    // meta data (if they have meta data).

    // the following are for assigning default mount points when no explicit
    // mount point is provided.
    std::vector<std::optional<std::string>> default_mounts{
        "/user-environment", "/user-tools", {}};
    auto default_mount = default_mounts.begin();

    std::vector<uenv::uenv_concretisation> uenvs;
    for (auto& desc : *uenv_descriptions) {
        // determine the mount point of the uenv, then validate that it exists.
        fs::path mount;
        if (auto m = desc.mount()) {
            mount = *m;
            // once an explicit mount point has been set defaults are no longer
            // applied. set default mount to the last entry, which is a nullopt
            // std::optional.
            default_mount = std::prev(default_mounts.end());
        } else {
            // no mount point was provided for this uenv, so use the default if
            // it is available.
            if (*default_mount) {
                mount = **default_mount;
                ++default_mount;
            } else {
                return util::unexpected(
                    fmt::format("no mount point provided for {}", desc));
            }
        }
        fmt::println("{} will be mounted at {}", desc, mount);

        // check that the mount point exists
        if (!fs::exists(mount)) {
            return util::unexpected(
                fmt::format("the mount point '{}' does not exist", mount));
        }

        if (desc.label()) {
            return util::unexpected(
                fmt::format("support for mounting uenv from labels is not "
                            "supported yet: '{}'",
                            *desc.label()));
        }

        // get the sqfs_path
        // desc.filename - check that it exists
        // desc.label - perform database lookup (TODO)

        // TODO when this code goes into a funtion, we need to parameterise
        // whether an absolute path is a hard requirement (for the SLURM plugin
        // it is)
        auto p = fs::path(*desc.filename());
        if (!fs::exists(p)) {
            return util::unexpected(fmt::format("{} does not exist", p));
        }
        auto sqfs_path = fs::absolute(p);
        if (!fs::is_regular_file(sqfs_path)) {
            return util::unexpected(fmt::format("{} is not a file", sqfs_path));
        }

        // get the meta data path
        auto meta_path = sqfs_path.parent_path() / "meta/env.json";

        // if meta_path exists, parse the json therein
        std::optional<fs::path> meta;
        if (fs::is_regular_file(meta_path)) {
            meta = meta_path;
        } else {
            fmt::println("the meta data file {} does not exist", meta_path);
        }
    }

    // step 2: parse the views
    //  - parse the CLI view description
    //  - look up view descriptions in the uenv meta data
    if (args.view_description) {
        auto views = uenv::parse_view_args(*args.view_description);
        if (!views) {
            return util::unexpected(
                fmt::format("invalid view description: {}", views.error().msg));
        }
        return start_settings{*uenv_descriptions, *views};
    }

    return start_settings{*uenv_descriptions, {}};
}

int start(const start_args& args, const global_settings& globals) {
    fmt::println("running start with options {}", args);
    const auto settings = parse_start_args(args);
    if (!settings) {
        fmt::print("ERROR\n{}\n", settings.error());
        return 1;
    }
    for (auto& v : settings->views) {
        fmt::print("  with view: {}\n", v);
    }
    return 0;
}

} // namespace uenv
