#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "util/expected.h"

namespace uenv {

util::expected<int, std::string> openat(int dirfd, std::filesystem::path file,
                                         int oflag);

util::expected<void, std::string> write(int fd, const void* buf, size_t count);

}  // namespace uenv
