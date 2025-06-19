#include <filesystem>

#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <site/site.h>
#include <uenv/parse.h>
#include <uenv/registry.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/subprocess.h>

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

util::expected<registry, std::string>
create_registry_from_config(const configuration& config) {
    if (!config.registry) {
        return util::unexpected("registry is not configured - set it in the "
                                "config file or provide --registry option");
    }

    auto registry_url = config.registry.value().string();

    switch (config.registry_type_val) {
    case registry_type::site:
        return site::create_site_registry();
    case registry_type::oci:
        return create_registry(registry_url, config.registry_type_val);
    }

    return util::unexpected("unknown registry type");
}

} // namespace uenv
