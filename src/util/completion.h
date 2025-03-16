#pragma once

#include <string>

#include <CLI/CLI.hpp>

namespace util {
namespace completion {

std::string bash_completion(CLI::App* cli, const std::string& name);

} // namespace completion
} // namespace util
