#include <filesystem>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/fs.h>
#include <util/shell.h>
#include <util/subprocess.h>

#include <uenv/parse.h>

#include "util.h"

namespace uenv {

// returns true if squashfs-mount 9 or later is detected in PATH.
// this is used for a compatibility layer
bool sqfs_mount_v9(const envvars::state& calling_environment) {
    if (auto PATH = calling_environment.get("PATH")) {
        if (auto exe = util::which("squashfs-mount", *PATH)) {
            if (auto run = util::run({exe->string(), "--version"})) {
                if (!run->wait()) {
                    if (auto output = run->out.getline()) {
                        if (auto version = uenv::parse_semver(output.value())) {
                            spdlog::debug("squashfs-mount version {}",
                                          *version);
                            return version->major >= 9;
                        }
                    }
                }
            } else {
                spdlog::warn("squashfs-mount is not in PATH");
            }
        }
    }
    return false;
}

std::vector<std::string>
squashfs_mount_args(const envvars::state& calling_environment,
                    const std::vector<std::string>& mounts,
                    const std::vector<std::string>& args) {
    std::vector<std::string> commands = {"squashfs-mount"};
    if (!sqfs_mount_v9(calling_environment)) {
        for (auto& m : mounts) {
            commands.push_back(m);
        }
    } else {
        commands.push_back(fmt::format("--sqfs={}", fmt::join(mounts, ",")));
    }
    commands.push_back("--");
    commands.insert(commands.end(), args.begin(), args.end());
    spdlog::debug("{}", fmt::join(commands, " "));
    return commands;
}

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
        img.meta = p.value() / "meta";
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
