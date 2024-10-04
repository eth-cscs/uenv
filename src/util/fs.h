#pragma once

#include <filesystem>
#include <string>

#include <util/expected.h>

namespace util {

class temp_dir {
    std::filesystem::path path_{};

  public:
    temp_dir() = delete;
    temp_dir(const temp_dir&) = delete;

    temp_dir(temp_dir&& p);
    temp_dir(std::filesystem::path p);

    const std::filesystem::path& path() const;

    ~temp_dir();
};

temp_dir make_temp_dir();

util::expected<temp_dir, std::string>
unsquashfs_tmp(const std::filesystem::path& sqfs,
               const std::filesystem::path& contents);

} // namespace util
