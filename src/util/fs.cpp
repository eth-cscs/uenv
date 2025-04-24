#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sys/file.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include "expected.h"
#include "fs.h"
#include "subprocess.h"


namespace util {

struct temp_dir_wrap {
    std::filesystem::path path;
    ~temp_dir_wrap() {
        if (std::filesystem::is_directory(path)) {
            // ignore the error code - being unable to delete a temp path is not
            // the end of the world.
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            //  warning: this might be called after spdlog is deactivated, so no
            //  logging!
        }
    }
};

// persistant storage for the temporary paths that will delete the paths on
// exit. This makes temporary paths persistent for the duration of the
// application's execution.
// Use a deque because it will not copy/move/delete its contents as it grows.
static std::deque<temp_dir_wrap> tmp_dir_cache;

// the temp paths are deleted automatically when tmp_dir_cache is cleaned up
// at the end of execution, except when execvp is used to replace the current
// process.
// use this function to force early clean up in such situations, so that no
// tmp paths remain after execution.
void clear_temp_dirs() {
    tmp_dir_cache.clear();
}

std::filesystem::path make_temp_dir() {
    namespace fs = std::filesystem;
    auto tmp_template =
        fs::temp_directory_path().string() + "/uenv-XXXXXXXXXXXX";
    std::vector<char> base(tmp_template.data(),
                           tmp_template.data() + tmp_template.size() + 1);

    fs::path tmp_path = mkdtemp(base.data());

    spdlog::debug("make_temp_dir: created {}", tmp_path.string(),
                  fs::is_directory(tmp_path));

    tmp_dir_cache.emplace_back(tmp_path);

    return tmp_path;
}

util::expected<std::filesystem::path, std::string>
unsquashfs_tmp(const std::filesystem::path& sqfs,
               const std::filesystem::path& contents) {
    namespace fs = std::filesystem;

    if (!fs::is_regular_file(sqfs)) {
        return unexpected(fmt::format("unsquashfs_tmp: {} file does not exist",
                                      sqfs.string()));
    }

    auto base = make_temp_dir();
    std::vector<std::string> command{"unsquashfs",  "-no-exit",
                                     "-d",          base.string(),
                                     sqfs.string(), contents.string()};
    spdlog::debug("unsquashfs_tmp: attempting to unpack {} from {}",
                  contents.string(), sqfs.string());

    auto proc = run(command);

    if (!proc) {
        return unexpected(fmt::format(
            "unsquashfs_tmp: unable to run unsquashfs: {}", proc.error()));
    }

    auto status = proc->wait();

    spdlog::debug("unsquashfs_tmp: command '{}' retured status {}",
                  fmt::join(command, " "), status);

    if (status != 0) {
        spdlog::warn("unsquashfs_tmp: unable to extract {} from {}",
                     contents.string(), sqfs.string());
        return unexpected(
            fmt::format("unsquashfs_tmp: unable to extract {} from {}",
                        contents.string(), sqfs.string()));
    }

    spdlog::info("unsquashfs_tmp: unpacked {} from {} to {}", contents.string(),
                 sqfs.string(), base.string());
    return base;
}

util::expected<std::tm, std::string>
file_creation_date(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    namespace cr = std::chrono;

    const auto creation_time = fs::last_write_time(path);

    // convert file_time_type to system clock time_point
    const auto sctp = cr::time_point_cast<cr::system_clock::duration>(
        creation_time - fs::file_time_type::clock::now() +
        cr::system_clock::now());

    // convert to time_t for easy manipulation
    std::time_t cftime = cr::system_clock::to_time_t(sctp);

    // extract the date components
    return *std::gmtime(&cftime);
}

util::expected<file_lock, std::string>
make_file_lock(const std::filesystem::path& path) {
    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        return unexpected{"unable to open file for locking"};
    }
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return unexpected{"unable to aquire file lock"};
    }

    return file_lock{fd};
}

// Allow move semantics
file_lock::file_lock(file_lock&& other) noexcept : fd(other.fd) {
    other.fd = -1;
}

file_lock& file_lock::operator=(file_lock&& other) noexcept {
    if (this != &other) {
        release();
        fd = other.fd;
        other.fd = -1;
    }
    return *this;
}

file_lock::~file_lock() {
    release();
}

void file_lock::release() {
    if (fd != -1) {
        flock(fd, LOCK_UN);
        close(fd);
        fd = -1;
    }
}

std::optional<std::filesystem::path> exe_path() {
    std::error_code ec;
    // /proc/self/exe is a symlink to the currently executing process in
    // posix-land
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);

    if (ec) {
        return std::nullopt;
    }

    return p;
}

std::optional<std::filesystem::path> oras_path() {
    namespace fs = std::filesystem;

    auto exe = exe_path();
    if (!exe) {
        return std::nullopt;
    }

    const auto prefix = exe->parent_path();

    for (auto& path : {"../libexec/oras", "oras"}) {
        const auto p = prefix / path;
        if (fs::is_regular_file(p)) {
            return fs::canonical(p);
        }
    }

    // maybe this could be extended to search PATH
    return std::nullopt;
}

file_level file_access_level(const std::filesystem::path& path) {
    namespace fs = std::filesystem;

    using enum file_level;
    std::error_code ec;
    auto status = fs::status(path, ec);

    if (ec) {
        spdlog::error("file_access_level {} error '{}'", path, ec.message());
        return none;
    }

    auto p = status.permissions();

    // check if the path is readable by the user, group, or others
    file_level lvl = none;
    constexpr auto pnone = std::filesystem::perms::none;
    if ((p & fs::perms::owner_read) != pnone ||
        (p & fs::perms::group_read) != pnone ||
        (p & fs::perms::others_read) != pnone) {
        spdlog::trace("file_access_level {} can be read", path, ec.message());
        lvl = readonly;
    }
    // check if the path is writable by the user, group, or others
    if ((p & fs::perms::owner_write) != pnone ||
        (p & fs::perms::group_write) != pnone ||
        (p & fs::perms::others_write) != pnone) {
        spdlog::trace("file_access_level {} can be written", path,
                      ec.message());
        lvl = readwrite;
    }
    return lvl;
}

} // namespace util
