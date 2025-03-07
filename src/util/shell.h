#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <util/expected.h>

namespace util {

/// returns the path of the shell currently being used
util::expected<std::filesystem::path, std::string> current_shell();

/// execve
int exec(const std::vector<std::string>& argv, char* const envp[] = nullptr);

} // namespace util
