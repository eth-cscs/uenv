#include <cerrno>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include "expected.h"
#include "fs.h"
#include "subprocess.h"
#include "unique_fd.h"

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

bool is_temp_dir(const std::filesystem::path& path) {
    for (const auto& p : tmp_dir_cache) {
        if (is_child(path, p.path)) {
            return true;
        }
    }
    return false;
}

util::expected<std::filesystem::path, std::string> make_temp_dir() {
    namespace fs = std::filesystem;
    auto tmp_template =
        fs::temp_directory_path().string() + "/uenv-XXXXXXXXXXXX";
    std::vector<char> base(tmp_template.data(),
                           tmp_template.data() + tmp_template.size() + 1);

    if (mkdtemp(base.data()) == nullptr) {
        return unexpected(fmt::format("unable to create a temporary directory "
                                      "from template {}: {}",
                                      tmp_template, strerror(errno)));
    }
    fs::path tmp_path = base.data();

    spdlog::debug("make_temp_dir: created {}", tmp_path.string(),
                  fs::is_directory(tmp_path));

    tmp_dir_cache.emplace_back(tmp_path);

    return tmp_path;
}

util::expected<void, std::string>
ensure_directory(const std::filesystem::path& path) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        return unexpected(fmt::format("unable to create {}: {}", path.string(),
                                      ec.message()));
    }

    if (!fs::is_directory(path, ec)) {
        return unexpected(
            fmt::format("{} exists but is not a directory", path.string()));
    }

    if (file_access_level(path) != file_level::readwrite) {
        return unexpected(
            fmt::format("the directory {} is not writable", path.string()));
    }

    return {};
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
    if (!base) {
        return unexpected(fmt::format("unsquashfs_tmp: {}", base.error()));
    }
    std::vector<std::string> command{
        "unsquashfs", "-no-exit", "-d", base->string(),
        // single threaded to avoid resource contention.
        "-processors", "1", sqfs.string(), contents.string()};
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
                 sqfs.string(), base->string());
    return *base;
}

util::expected<std::tm, std::string>
file_creation_date(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    namespace cr = std::chrono;

    std::error_code ec;
    const auto creation_time = fs::last_write_time(path, ec);
    if (ec) {
        return util::unexpected{
            fmt::format("unable to read the modification time of {}: {}",
                        path.string(), ec.message())};
    }

    // convert file_time_type to system clock time_point
    const auto sctp = cr::time_point_cast<cr::system_clock::duration>(
        creation_time - fs::file_time_type::clock::now() +
        cr::system_clock::now());

    // convert to time_t for easy manipulation
    std::time_t cftime = cr::system_clock::to_time_t(sctp);

    // extract the date components
    std::tm date{};
    if (gmtime_r(&cftime, &date) == nullptr) {
        return util::unexpected{fmt::format(
            "the modification time of {} is out of range", path.string())};
    }
    return date;
}

namespace {
std::filesystem::file_status status_or_log(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec && status.type() != std::filesystem::file_type::not_found) {
        spdlog::warn("unable to examine {}: {}", path.string(), ec.message());
    }
    return status;
}
} // namespace

bool path_exists(const std::filesystem::path& path) {
    return std::filesystem::exists(status_or_log(path));
}

bool path_is_file(const std::filesystem::path& path) {
    return std::filesystem::is_regular_file(status_or_log(path));
}

bool path_is_dir(const std::filesystem::path& path) {
    return std::filesystem::is_directory(status_or_log(path));
}

util::expected<std::filesystem::path, std::string>
absolute_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec) {
        return util::unexpected{fmt::format("unable to make {} absolute: {}",
                                            path.string(), ec.message())};
    }
    return abs;
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

std::optional<std::string>
read_single_line_file(const std::filesystem::path& path) {
    if (auto file = std::ifstream{path}) {
        std::string line;
        if (std::getline(file, line)) {
            return line;
        }
    }
    return std::nullopt;
}

util::expected<std::string, std::string>
read_file(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return util::unexpected{fmt::format(
            "unable to open {}: {}", path.string(), std::strerror(errno))};
    }
    std::string content{std::istreambuf_iterator<char>{file},
                        std::istreambuf_iterator<char>{}};
    if (file.bad()) {
        return util::unexpected{fmt::format(
            "unable to read {}: {}", path.string(), std::strerror(errno))};
    }
    return content;
}

std::optional<std::string> read_regular_file(const std::filesystem::path& path,
                                             std::uintmax_t max_size) {
    util::unique_fd fd(::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    struct stat st{};
    if (!fd || ::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) ||
        static_cast<std::uintmax_t>(st.st_size) > max_size) {
        return std::nullopt;
    }
    std::string contents(static_cast<std::size_t>(st.st_size), '\0');
    std::size_t done = 0;
    while (done < contents.size()) {
        auto n =
            ::read(fd.get(), contents.data() + done, contents.size() - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return std::nullopt;
        }
        done += static_cast<std::size_t>(n);
    }
    return contents;
}

util::expected<void, std::string>
write_file_atomic(const std::filesystem::path& path,
                  std::string_view contents) {
    namespace fs = std::filesystem;

    const auto tmp =
        path.parent_path() /
        fmt::format(".{}.{}", path.filename().string(), ::getpid());
    bool written = false;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(contents.data(),
                  static_cast<std::streamsize>(contents.size()));
        out.close();
        written = !out.fail();
    }
    std::error_code ec;
    if (!written) {
        fs::remove(tmp, ec);
        return util::unexpected{
            fmt::format("unable to write {}", tmp.string())};
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        const auto msg = fmt::format("unable to rename {} to {}: {}",
                                     tmp.string(), path.string(), ec.message());
        fs::remove(tmp, ec);
        return util::unexpected{msg};
    }
    return {};
}

bool touch(const std::filesystem::path& path) {
    util::unique_fd fd(
        ::open(path.c_str(),
               O_WRONLY | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0644));
    return fd && ::futimens(fd.get(), nullptr) == 0;
}

bool modified_within(const std::filesystem::path& path,
                     std::filesystem::file_time_type::duration age) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto modified = fs::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    const auto elapsed = fs::file_time_type::clock::now() - modified;
    return elapsed < age && elapsed > -age;
}

bool is_child(const std::filesystem::path& child,
              const std::filesystem::path& parent) {
    auto rel = child.lexically_relative(parent);
    return !rel.empty() && *rel.begin() != "..";
}

} // namespace util
