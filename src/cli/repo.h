// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <optional>
#include <string>

#include <CLI/CLI.hpp>

#include "uenv.h"

namespace uenv {

struct repo_create_args {
    std::optional<std::string> path;
};
struct repo_status_args {
    std::optional<std::string> path;
    // print output in json format
    bool json = false;
};
struct repo_update_args {
    std::optional<std::string> path;
    // whether to apply lustre checks
    bool lustre = true;
};
struct repo_migrate_args {
    // takes two arguments: source and destination
    // source is optional, but comes first when provided
    // so we take two unnamed arguments, and determine them later
    std::optional<std::string> path0;
    std::optional<std::string> path1;
    bool sync = true;
};

void repo_help();

struct repo_args {
    repo_create_args create_args;
    repo_migrate_args migrate_args;
    repo_status_args status_args;
    repo_update_args update_args;

    void add_cli(CLI::App&, global_settings& settings);
};

int repo_create(const repo_create_args& args, const global_settings& settings);
int repo_status(const repo_status_args& args, const global_settings& settings);
int repo_update(const repo_update_args& args, const global_settings& settings);
int repo_migrate(const repo_migrate_args& args,
                 const global_settings& settings);

} // namespace uenv
