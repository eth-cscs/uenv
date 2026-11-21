#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <util/expected.h>

namespace util {

util::expected<std::filesystem::path, std::string> make_temp_dir();

bool is_temp_dir(const std::filesystem::path& path);

// Ensure `path` is a writable directory, creating it (and any missing parents)
// if necessary. Succeeds only when the final path exists, is a directory, and
// is writable.
util::expected<void, std::string>
ensure_directory(const std::filesystem::path& path);

util::expected<std::filesystem::path, std::string>
unsquashfs_tmp(const std::filesystem::path& sqfs,
               const std::filesystem::path& contents);

void clear_temp_dirs();

util::expected<std::tm, std::string>
file_creation_date(const std::filesystem::path& path);

// Non-throwing tests on a path, which follow symlinks.
//
// The std::filesystem functions without an std::error_code throw when a path
// can't be examined (EACCES on a parent directory, ENAMETOOLONG, a stale
// network file system), which terminates the program.
//
// fs::status with an std::error_code is easy to get wrong: it sets the error
// code for a path that does not exist (ENOENT, ENOTDIR) as well as for one
// that can't be examined. path_type separates the two: a path that does not
// exist has the type file_type::not_found, and only a path that can't be
// examined is an error.
util::expected<std::filesystem::file_type, std::error_code>
path_type(const std::filesystem::path& path);

// Check that a path is a regular file or a directory. The error says why it
// isn't, as the end of a sentence about the path, for example
// fmt::format("the mount point {} {}", path, r.error()):
//   "does not exist"
//   "is not a regular file" or "is not a directory"
//   "can't be accessed: Permission denied" (the reason given by the system)
// Use these where the reason is reported to the user.
util::expected<void, std::string> check_file(const std::filesystem::path& path);
util::expected<void, std::string> check_dir(const std::filesystem::path& path);

// Return false for a path that can't be examined, and log the reason with
// spdlog::warn. Logging is off by default, so the reason is not seen by the
// user: use check_file/check_dir where it should be.
bool path_exists(const std::filesystem::path& path);
bool path_is_file(const std::filesystem::path& path);
bool path_is_dir(const std::filesystem::path& path);

// std::filesystem::absolute, reporting a failure (the current directory
// can't be determined) as an error instead of throwing.
util::expected<std::filesystem::path, std::string>
absolute_path(const std::filesystem::path& path);

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

// read the full contents of a text file into a string.
util::expected<std::string, std::string>
read_file(const std::filesystem::path& path);

// The contents of a regular file of at most `max_size` bytes. Nothing else is
// read, and the read never blocks: a FIFO put in place of the file would
// otherwise hang the reader. Returns nullopt for anything else.
std::optional<std::string> read_regular_file(const std::filesystem::path& path,
                                             std::uintmax_t max_size);

// Write `contents` to `path` so that a reader sees either the old file or the
// whole of the new one: the contents are written to .<filename>.<pid> in the
// same directory, which is then renamed over `path`. The temporary file is
// removed on error.
util::expected<void, std::string>
write_file_atomic(const std::filesystem::path& path, std::string_view contents);

// create `path` if it does not exist, and set its modification time to now.
// Symbolic links are not followed.
bool touch(const std::filesystem::path& path);

// Whether `path` was modified within `age` of now. A time further than `age`
// in the future, from a clock that was wrong, is not within it, so that it
// can't keep a file recent for ever.
bool modified_within(const std::filesystem::path& path,
                     std::filesystem::file_time_type::duration age);

// return if a path is inside a directory, i.e. direct or indirect child, or
// the directory itself. Decided by path component (not string prefix)
// comparison: "/data2" is not inside "/data".
bool is_child(const std::filesystem::path& child,
              const std::filesystem::path& parent);

} // namespace util
