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

class file_lock {
  public:
    file_lock(const file_lock&) = delete;
    file_lock& operator=(const file_lock&) = delete;

    file_lock(file_lock&& other) noexcept;
    file_lock& operator=(file_lock&& other) noexcept;

    ~file_lock();

    friend util::expected<file_lock, std::string>
    make_file_lock(const std::filesystem::path& path);

  private:
    int fd = -1;
    file_lock(int fd) : fd(fd) {
    }

    void release();
};

util::expected<file_lock, std::string>
make_file_lock(const std::filesystem::path& path);

// return the path of the current executable
// returns empty if there is an error
std::optional<std::filesystem::path> exe_path();

// return the path of the oras executable
// returns empty if it can't be found
std::optional<std::filesystem::path> oras_path();

// for determining the level of access to a file or directory
// if there is an error, or the file does not exist `none` is
// returned.
enum class file_level { none = 0, readonly = 1, readwrite = 2 };
file_level file_access_level(const std::filesystem::path& path);

// parse a file that is expected to contain a single line of text
// returns nullopt if the file does not exist, or contains no line.
// content is stripped of newline.
std::optional<std::string>
read_single_line_file(const std::filesystem::path& path);

// return if a path is inside a directory, i.e. direct or indirect child
bool is_child(const std::filesystem::path& child,
              const std::filesystem::path& parent);

} // namespace util
