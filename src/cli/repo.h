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
    bool json = false;
};

void repo_help();

struct repo_args {
    repo_create_args create_args;
    repo_status_args status_args;
    void add_cli(CLI::App&, global_settings& settings);
};

int repo_create(const repo_create_args& args, const global_settings& settings);
int repo_status(const repo_status_args& args, const global_settings& settings);
int repo_update(const repo_status_args& args, const global_settings& settings);

} // namespace uenv
