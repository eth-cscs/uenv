#pragma once

#include <CLI/CLI.hpp>

namespace util {
namespace completion {

std::string bash_completion(CLI::App* cli);

} // namespace completion
} // namespace util
