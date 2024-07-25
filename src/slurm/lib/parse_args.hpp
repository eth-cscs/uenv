#pragma once

#include <optional>
#include <string>
#include <vector>

#include <lib/expected.hpp>
#include <lib/mount.hpp>

namespace util {

enum class protocol { file, https, jfrog };

/// parse list of `:` separated tuples, enforce absolute paths, sort output by
/// mountpoint. The error state is an error string.
util::expected<std::vector<util::mount_entry>, std::string>
parse_arg(const std::string &arg,
          std::optional<std::string> uenv_repo_path = std::nullopt,
          std::optional<std::string> uenv_arch = std::nullopt);

} // namespace util
