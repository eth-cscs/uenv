// vim: ts=4 sts=4 sw=4 et

#include <vector>
#include <string>
#include <filesystem>

#include <fmt/core.h>

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

    // step 1: parse the uenv information
    //  inputs: (uenv cli argument)
    //  - parse the CLI uenv description
    //  inputs: (global settings, parsed cli argument)
    const auto uenv_descriptions = uenv::parse_uenv_args(args.uenv_description);
    if (!uenv_descriptions) {
        return util::unexpected(
            fmt::format("unable to read the uenv argument\n  {}",
                        uenv_descriptions.error().msg));
    }

    //  - lookup either file name or database (the filename can be relative in
    //  uenv, but absolute in slurm)
    //  - generate uenv meta data
    std::vector<uenv::uenv_concretisation> uenvs;
    std::vector<std::optional<std::string>> default_mounts{"/user-environment", "/user-tools", {}};
    // points to the mount point that should used if none is provided for a uenv
    auto default_mount = default_mounts.begin();
    for (auto& desc : *uenv_descriptions) {
        fmt::println("distance={}", std::distance(default_mounts.begin(), default_mount));
        // mount will hold the mount point as a string.
        std::string mount;
        if (auto m = desc.mount()) {
            mount = *m;
            // once an explicit mount point has been set defaults are no longer applied.
            // set default mount to the last entry, which is a nullopt std::optional.
            default_mount = std::prev(default_mounts.end());
        }
        else {
            // no mount point was provided for this uenv, so use the default if it is available.
            if (*default_mount) {
                mount = **default_mount;
                ++default_mount;
            }
            else {
                return util::unexpected(fmt::format("no mount point provided for {}", desc));
            }
        }
        fmt::println("{} will be mounted at {}", desc, mount);


        // check that the mount point exists
        if (!fs::exists(mount)) {
            return util::unexpected(fmt::format("the mount point '{}' does not exist", desc));
        }


        if (desc.label()) {}
        // get the sqfs_path
        // desc.filename - check that it exists
        // desc.label - perform database lookup

        // meta_path = sqfs_path.parent_path
        // check whether meta_path exists

        // if meta_path exists, parse the json therin for information about
        // views create a hash table lookup from name -> view_meta
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
