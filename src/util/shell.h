#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <util/environment.h>
#include <util/expected.h>

namespace util {

/// returns the path of the current shell
util::expected<std::filesystem::path, std::string>
current_shell(const environment::variables&);

/// execve
int exec(const std::vector<std::string>& argv, char* const envp[] = nullptr);

} // namespace util
