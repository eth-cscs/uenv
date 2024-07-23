#pragma once

// #include <filesystem>
// #include <optional>
// #include <string>
// #include <variant>
#include <string>
#include <unordered_map>
#include <vector>

// #include <fmt/core.h>

#include <util/expected.h>

#include <uenv/uenv.h>
#include <uenv/view.h>

namespace uenv {

struct env {
    std::unordered_map<std::string, concrete_uenv> uenvs;

    // the order of views matters: views are initialised in order
    std::vector<qualified_view_description> views;
};

util::expected<env, std::string>
concretise_env(const std::string& uenv_args,
               std::optional<std::string> view_args);

} // namespace uenv
