#pragma once

#include <optional>
#include <filesystem>
#include <util/expected.h>
#include "uenv/uenv.h"

namespace uenv {


struct modular_env_paths {
  std::filesystem::path sqfs_path;
  std::filesystem::path mount_path;
  std::vector<std::tuple<std::filesystem::path, std::filesystem::path>> sub_images;
};

util::expected<modular_env_paths, std::string>
read_modular_env(const std::filesystem::path&,
                 std::optional<std::filesystem::path>);

} // namespace uenv
