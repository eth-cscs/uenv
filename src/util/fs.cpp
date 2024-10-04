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

temp_dir::~temp_dir() {
    std::error_code ec;

    if (std::filesystem::is_directory(path_)) {
        // ignore the error code - being unable to delete a temp path is not the
        // end of the world.
        auto n = std::filesystem::remove_all(path_, ec);
        spdlog::debug("temp_dir: deleted {} files in {}", n, path_.string());
    }

    // path_ is either set, or default constructed as
    //   std::filesystem::path::empty
    //   https://en.cppreference.com/w/cpp/filesystem/path/empty deleting an
    //   empty
    // path is safe - it is a noop.
}

temp_dir::temp_dir(temp_dir&& p) : path_(std::move(p.path_)) {
}

temp_dir::temp_dir(std::filesystem::path p) : path_(std::move(p)) {
}

const std::filesystem::path& temp_dir::path() const {
    return path_;
}

temp_dir make_temp_dir() {
    namespace fs = std::filesystem;
    auto tmp_template =
        fs::temp_directory_path().string() + "/uenv-XXXXXXXXXXXX";
    std::vector<char> base(tmp_template.data(),
                           tmp_template.data() + tmp_template.size() + 1);

    fs::path tmp_path = mkdtemp(base.data());

    spdlog::debug("make_temp_dir: created {}", tmp_path.string(),
                  fs::is_directory(tmp_path));

    return tmp_path;
}

util::expected<temp_dir, std::string>
unsquashfs_tmp(const std::filesystem::path& sqfs,
               const std::filesystem::path& contents) {

    namespace fs = std::filesystem;

    if (!fs::is_regular_file(sqfs)) {
        return unexpected(fmt::format("unsquashfs_tmp: {} file does not exist",
                                      sqfs.string()));
    }

    auto base = make_temp_dir();
    std::vector<std::string> command{"unsquashfs",  "-no-exit",
                                     "-d",          base.path().string(),
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

    spdlog::info("unsquashfs_tmp: data unpacked to {}", base.path().string());
    return base;
}
} // namespace util
