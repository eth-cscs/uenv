// vim: ts=4 sts=4 sw=4 et

#pragma once

#include <CLI/CLI.hpp>

namespace util {
namespace completion {

std::string bash_completion(CLI::App* cli);

} // namespace completion
} // namespace util
