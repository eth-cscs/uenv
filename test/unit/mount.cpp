#include <filesystem>
#include <fstream>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>
#include <fmt/std.h>

#include <uenv/mount.h>
#include <util/fs.h>

namespace fs = std::filesystem;

TEST_CASE("validate_mounts", "[mount]") {
    // create some "fake" squashfs images
    // squashfs images start with the magic 4 charachters 'hsqs'
    auto sqfs_root = util::make_temp_dir();
    auto sqfs_1 = sqfs_root / "sqfs1.squashfs";
    auto sqfs_2 = sqfs_root / "sqfs2.squashfs";
    auto sqfs_3 = sqfs_root / "sqfs3.squashfs";
    auto sqfs_4 = sqfs_root / "sqfs4.squashfs";
    for (auto f : {sqfs_1, sqfs_2, sqfs_3, sqfs_4}) {
        std::ofstream{f} << "hsqsx";
    }
    // create some fake squashfs
    auto mount = util::make_temp_dir();
    auto mount_a_b = mount / "a" / "b";
    auto mount_b = mount / "b";
    auto mount_a = mount / "a";
    auto mount_other = util::make_temp_dir();

    // a single mount point that exists
    REQUIRE(
        uenv::parse_and_validate_mounts(fmt::format("{}:{}", sqfs_1, mount)));
    // two mount points, with one existing and the other "inside"
    REQUIRE(uenv::parse_and_validate_mounts(
        fmt::format("{}:{},{}:{}", sqfs_1, mount, sqfs_2, mount_a)));
    // same as previous, but with mount point order reversed
    REQUIRE(uenv::parse_and_validate_mounts(
        fmt::format("{}:{},{}:{}", sqfs_1, mount_a, sqfs_2, mount)));
    REQUIRE(uenv::parse_and_validate_mounts(
        fmt::format("{}:{},{}:{},{}:{},{}:{}", sqfs_1, mount, sqfs_2, mount_a,
                    sqfs_3, mount_a_b, sqfs_4, mount_b)));
    // a single mount point that does not exist
    REQUIRE(!uenv::parse_and_validate_mounts(
        fmt::format("{}:{}", sqfs_1, mount_a)));
}
