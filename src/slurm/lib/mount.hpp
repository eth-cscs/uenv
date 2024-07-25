#pragma once

#include <string>
#include <vector>

#include "expected.hpp"

#define UENV_MOUNT_LIST "UENV_MOUNT_LIST"

namespace util {

struct mount_entry {
  // enum protocol p;
  std::string image_path;
  std::string mount_point;
};

/// check if mountpoint is an existent directory
bool is_valid_mountpoint(const std::string &mount_point);

/// mount images and export env variable UENV_MOUNT_LIST
util::expected<std::string, std::string>
do_mount(const std::vector<mount_entry> &mount_entries);

} // namespace util
