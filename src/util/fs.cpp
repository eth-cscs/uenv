#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
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
            auto n = std::filesystem::remove_all(path, ec);
            spdlog::debug("temp_dir_wrap: deleted {} files in {}", n,
                          path.string());
        }
    }
};

// persistant storage for the temporary paths that will delete the paths on
// exit. This makes temporary paths persistent for the duration of the
// application's execution.
// Use a dequeu because it will not copy/move/delete its contents as it grows.
static std::deque<temp_dir_wrap> tmp_dir_cache;

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
} // namespace util
