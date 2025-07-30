#include <cstring>
#include <err.h>
#include <fmt/format.h>
#include <sched.h>
extern "C" {
#include <squashfuse/ll.h>
#include <sys/syscall.h>
#include <unistd.h>
}
#include <filesystem>
#include <spdlog/spdlog.h>
#include <stddef.h>
#include <string>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <uenv/mount.h>
#include <unistd.h>
#include <util/expected.h>

namespace fs = std::filesystem;
namespace uenv {

/*
 * see https://github.com/containers/bubblewrap/blob/main/bind-mount.c#L378
 */
util::expected<void, std::string> make_mutable_root() {
    auto original_path = fs::current_path();
    spdlog::info("make mutable root");
    std::vector<fs::path> paths;
    std::vector<std::pair<fs::path, fs::path>> symlinks;
    // iterate over root dirs
    for (const auto& entry : fs::directory_iterator("/")) {
        if (entry.is_directory() && !entry.is_symlink()) {
            // spdlog::info("entry {}", entry.path().c_str());
            paths.push_back(entry);
        }
        if (entry.is_directory() && entry.is_symlink()) {
            auto dest = fs::read_symlink(entry);
            symlinks.push_back(std::make_pair(entry, dest));
        }
    }

    if (auto r = uenv::mount(std::nullopt, "/", std::nullopt,
                             MS_REC | MS_SILENT | MS_SLAVE, nullptr);
        !r) {
        return r;
    }

    if (auto r = uenv::mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV,
                             nullptr);
        !r) {
        return r;
    }

    fs::current_path("/tmp");
    fs::create_directory("newroot");

    if (auto r =
            uenv::mount("newroot", "newroot", std::nullopt,
                        MS_MGC_VAL | MS_BIND | MS_REC | MS_SILENT, nullptr);
        !r) {
        return r;
    }
    fs::create_directory("oldroot");

    if (syscall(SYS_pivot_root, "/tmp", "oldroot") != 0) {
        return util::unexpected{
            fmt::format("{} {}: {}", __FILE__, __LINE__, strerror(errno))};
    }
    fs::current_path("/");

    // 1. symlinks
    /* What bwrap does;
     * mount("/oldroot/usr/bin", "/newroot/bin", NULL, MS_BIND|MS_REC|MS_SILENT,
     * NULL) = 0 mount("none", "/newroot/bin", NULL,
     * MS_NOSUID|MS_REMOUNT|MS_BIND|MS_SILENT|MS_RELATIME, NULL) = 0
     */
    for (auto entry : symlinks) {
        auto src = fs::path("/oldroot") / entry.second.relative_path();
        auto dst = fs::path("/newroot") / entry.first.relative_path();

        spdlog::debug("src {}, dst {}", src.c_str(), dst.c_str());
        spdlog::debug("fs::create_directories {}", dst.c_str());
        fs::create_directories(dst);
        if (auto r = uenv::mount(src, dst, std::nullopt,
                                 MS_BIND | MS_REC | MS_SILENT, nullptr);
            !r) {
            return r;
        }

        if (auto r = uenv::mount("none", dst.c_str(), std::nullopt,
                                 MS_NOSUID | MS_REMOUNT | MS_BIND | MS_SILENT |
                                     MS_RELATIME,
                                 nullptr);
            !r) {
            return r;
        }
    }

    // 2. the rest
    for (auto entry : paths) {
        auto src = fs::path("/oldroot") / entry.relative_path();
        auto dst = fs::path("/newroot") / entry.relative_path();
        fs::create_directory(dst);
        if (auto r = uenv::mount(src.c_str(), dst.c_str(), std::nullopt,
                                 MS_BIND | MS_REC | MS_SILENT, nullptr);
            !r) {
            return r;
        }

        // TODO
        // // this fails for example for /tmp (tmpfs)
        // if (auto r = mount("none", dst.c_str(), std::nullopt,
        //                    MS_NOSUID | MS_REMOUNT | MS_BIND | MS_SILENT |
        //                        MS_RELATIME,
        //                    nullptr);
        //     !r) {
        //     return r;
        // }
    }
    if (auto r = uenv::mount("oldroot", "oldroot", std::nullopt,
                             MS_REC | MS_SILENT | MS_PRIVATE, nullptr);
        !r) {
        return r;
    }
    if (umount2("oldroot", MNT_DETACH) != 0) {
        return util::unexpected{
            fmt::format("{} {}: {}", __FILE__, __LINE__, strerror(errno))};
    }
    fs::current_path("/newroot");

    if (syscall(SYS_pivot_root, ".", ".") != 0) {
        return util::unexpected{
            fmt::format("{} {}: {}", __FILE__, __LINE__, strerror(errno))};
    }
    if (umount2(".", MNT_DETACH) != 0) {
        return util::unexpected{
            fmt::format("{} {}: {}", __FILE__, __LINE__, strerror(errno))};
    }
    fs::current_path(original_path);

    return {};
}

} // namespace uenv
