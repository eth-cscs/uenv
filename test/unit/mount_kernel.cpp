// Tests for the kernel mounting backend's access check.
//
// open_images() is what decides whether the calling user may read a squashfs
// image: the descriptors it returns are bound to the loop devices, so the
// check the kernel makes at open(2) is the check that governs the mount.
//
// Nothing here needs privilege. An ordinary user cannot read their own
// mode-000 file, nor traverse their own mode-000 directory, so both halves of
// the bypass this guards against are reachable unprivileged. Running as root
// bypasses exactly what is under test, so those cases skip.

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>
#include <fmt/std.h>

#include <uenv/mount.h>
#include <uenv/mount_kernel.h>
#include <util/fs.h>

namespace fs = std::filesystem;

namespace {

// a file that passes the squashfs magic check
void write_image(const fs::path& path) {
    std::ofstream{path} << "hsqsx";
}

uenv::mount_list one_mount(const fs::path& sqfs, const fs::path& mount) {
    return uenv::mount_list{uenv::mount_pair{.sqfs = sqfs, .mount = mount}};
}

} // namespace

TEST_CASE("open_images opens readable images", "[mount][kernel]") {
    auto root = util::make_temp_dir().value();
    auto first = root / "first.squashfs";
    auto second = root / "second.squashfs";
    write_image(first);
    write_image(second);
    auto mount_a = util::make_temp_dir().value();
    auto mount_b = util::make_temp_dir().value();

    auto images = uenv::open_images(
        uenv::mount_list{uenv::mount_pair{.sqfs = first, .mount = mount_a},
                         uenv::mount_pair{.sqfs = second, .mount = mount_b}});
    REQUIRE(images.has_value());
    REQUIRE(images->size() == 2);

    // the order of the mount list is preserved: validate_mount_list has
    // already sorted it so that a parent mount precedes anything nested
    // inside it, and do_mount relies on that order.
    REQUIRE(images->at(0).sqfs == first);
    REQUIRE(images->at(0).mount == mount_a);
    REQUIRE(images->at(1).sqfs == second);
    REQUIRE(images->at(1).mount == mount_b);

    for (const auto& image : *images) {
        REQUIRE(image.fd);
        // the descriptor must not leak into the command that is exec'd after
        // the mount
        const int flags = fcntl(image.fd.get(), F_GETFD);
        REQUIRE(flags != -1);
        REQUIRE((flags & FD_CLOEXEC) != 0);
    }
}

TEST_CASE("open_images refuses an image that cannot be read",
          "[mount][kernel]") {
    if (geteuid() == 0) {
        SKIP("running as root: file modes are bypassed");
    }
    auto root = util::make_temp_dir().value();
    auto sqfs = root / "unreadable.squashfs";
    write_image(sqfs);
    fs::permissions(sqfs, fs::perms::none);
    auto mount = util::make_temp_dir().value();

    auto images = uenv::open_images(one_mount(sqfs, mount));
    REQUIRE(!images.has_value());
    REQUIRE(images.error().find("Permission denied") != std::string::npos);
    REQUIRE(images.error().find(sqfs.string()) != std::string::npos);
}

TEST_CASE("open_images refuses an image in a directory that cannot be "
          "traversed",
          "[mount][kernel]") {
    if (geteuid() == 0) {
        SKIP("running as root: directory permissions are bypassed");
    }
    auto root = util::make_temp_dir().value();
    auto closed = root / "closed";
    fs::create_directory(closed);
    auto sqfs = closed / "image.squashfs";
    write_image(sqfs);
    // world-readable image, unreachable directory: this is the half of the
    // bypass that a file-mode check alone would miss.
    fs::permissions(sqfs, fs::perms::all);
    fs::permissions(closed, fs::perms::none);
    auto mount = util::make_temp_dir().value();

    auto images = uenv::open_images(one_mount(sqfs, mount));

    fs::permissions(closed,
                    fs::perms::owner_all); // so the temp dir can be removed

    REQUIRE(!images.has_value());
    REQUIRE(images.error().find("Permission denied") != std::string::npos);
}

TEST_CASE("open_images refuses an image that does not exist",
          "[mount][kernel]") {
    auto root = util::make_temp_dir().value();
    auto mount = util::make_temp_dir().value();

    auto images = uenv::open_images(one_mount(root / "absent.squashfs", mount));
    REQUIRE(!images.has_value());
    REQUIRE(images.error().find("No such file") != std::string::npos);
}

TEST_CASE("parse_and_validate_mounts reports a permission error as such",
          "[mount]") {
    if (geteuid() == 0) {
        SKIP("running as root: file modes are bypassed");
    }
    auto root = util::make_temp_dir().value();
    auto sqfs = root / "unreadable.squashfs";
    write_image(sqfs);
    fs::permissions(sqfs, fs::perms::none);
    auto mount = util::make_temp_dir().value();

    auto mounts =
        uenv::parse_and_validate_mounts(fmt::format("{}:{}", sqfs, mount));
    REQUIRE(!mounts.has_value());
    // an image the caller may not read is not the same thing as an image that
    // is not a regular file, and saying so sends users hunting for the wrong
    // problem.
    REQUIRE(mounts.error().find("Permission denied") != std::string::npos);
    REQUIRE(mounts.error().find("not a regular file") == std::string::npos);
}
