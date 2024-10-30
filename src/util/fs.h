#pragma once

#include <ctime>
#include <filesystem>
#include <string>

#include <util/expected.h>

namespace util {

std::filesystem::path make_temp_dir();

util::expected<std::filesystem::path, std::string>
unsquashfs_tmp(const std::filesystem::path& sqfs,
               const std::filesystem::path& contents);

void clear_temp_dirs();

util::expected<std::tm, std::string>
file_creation_date(const std::filesystem::path& path);

} // namespace util
