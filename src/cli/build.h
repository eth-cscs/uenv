#pragma once

#include "cli/uenv.h"
#include <CLI/App.hpp>

namespace uenv {
struct build_args {
    bool spack_develop = false;
    std::string uenv_recipe_path;
    std::string uenv_label;
    std::optional<std::string> system;
    void add_cli(CLI::App& cli, [[maybe_unused]] global_settings& settings);
};

int build(const build_args& args,
          [[maybe_unused]] const global_settings& settings);

} // namespace uenv
