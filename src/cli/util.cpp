#include <filesystem>

#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/fs.h>
#include <util/subprocess.h>

#include <uenv/parse.h>

#include "util.h"

namespace uenv {

util::expected<squashfs_image, std::string>
validate_squashfs_image(const std::string& path) {
    namespace fs = std::filesystem;

    squashfs_image img{};

    auto file = uenv::parse_path(path);
    if (!file) {
        return util::unexpected{fmt::format("invalid squashfs file {}: {}",
                                            path, file.error().message())};
    }
    img.sqfs = *file;
    if (!fs::is_regular_file(img.sqfs)) {
        return util::unexpected{
            fmt::format("invalid squashfs: {} is not a file", path)};
    }
    img.sqfs = fs::absolute(img.sqfs);
    spdlog::info("found squashfs {}", img.sqfs);

    if (auto p = util::unsquashfs_tmp(img.sqfs, "meta")) {
        img.meta = *p;
    } else {
        spdlog::info("no meta data in {}", img.sqfs);
    }

    auto proc = util::run({"sha256sum", img.sqfs.string()});
    if (!proc) {
        spdlog::error("{}", proc.error());
        return util::unexpected{fmt::format(
            "unable to calculate sha256 of squashfs file {}", img.sqfs)};
    }
    auto success = proc->wait();
    if (success != 0) {
        return util::unexpected{fmt::format(
            "unable to calculate sha256 of squashfs file {}", img.sqfs)};
    }
    auto raw = *proc->out.getline();
    img.hash = raw.substr(0, 64);

    return img;
}

} // namespace uenv
