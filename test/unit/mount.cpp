#include <filesystem>
#include <fstream>
#include <optional>

#include <unistd.h>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>
#include <fmt/std.h>

#include <uenv/mount.h>
#include <util/defer.h>
#include <util/fs.h>

// namespace fs = std::filesystem;

TEST_CASE("validate_mounts", "[mount]") {
    // create some "fake" squashfs images
    // squashfs images start with the magic 4 charachters 'hsqs'
    auto sqfs_root = util::make_temp_dir().value();
    auto sqfs_1 = sqfs_root / "sqfs1.squashfs";
    auto sqfs_2 = sqfs_root / "sqfs2.squashfs";
    auto sqfs_3 = sqfs_root / "sqfs3.squashfs";
    auto sqfs_4 = sqfs_root / "sqfs4.squashfs";
    for (auto f : {sqfs_1, sqfs_2, sqfs_3, sqfs_4}) {
        std::ofstream{f} << "hsqsx";
    }
    // create some fake squashfs
    auto mount = util::make_temp_dir().value();
    auto mount_a_b = mount / "a" / "b";
    auto mount_b = mount / "b";
    auto mount_a = mount / "a";
    auto mount_other = util::make_temp_dir().value();

    // valid inputs
    for (auto input : {
             // a single mount point that exists
             fmt::format("{}:{}", sqfs_1, mount),
             // two mount points, with one existing and the other "inside"
             fmt::format("{}:{},{}:{}", sqfs_1, mount, sqfs_2, mount_a),
             // same as previous, but with mount point order reversed
             fmt::format("{}:{},{}:{}", sqfs_1, mount_a, sqfs_2, mount),
             // two separate mount points that exist
             fmt::format("{}:{},{}:{}", sqfs_1, mount, sqfs_2, mount_other),
             // a more complicated example
             fmt::format("{}:{},{}:{},{}:{},{}:{}", sqfs_1, mount, sqfs_2,
                         mount_a, sqfs_3, mount_a_b, sqfs_4, mount_b),
         }) {
        REQUIRE(uenv::parse_and_validate_mounts(input));
    }
    // invalid inputs
    for (auto input : {
             // a single mount point that does not exist
             fmt::format("{}:{}", sqfs_1, mount_a),
             // two mount points: the second does not exist
             fmt::format("{}:{},{}:{}", sqfs_1, mount_other, sqfs_2, mount_a),
         }) {
        REQUIRE(!uenv::parse_and_validate_mounts(input));
    }
}

TEST_CASE("validate_mounts shared mount points", "[mount]") {
    auto sqfs_root = util::make_temp_dir().value();
    auto sqfs_1 = sqfs_root / "sqfs1.squashfs";
    auto sqfs_2 = sqfs_root / "sqfs2.squashfs";
    auto sqfs_3 = sqfs_root / "sqfs3.squashfs";
    for (auto f : {sqfs_1, sqfs_2, sqfs_3}) {
        std::ofstream{f} << "hsqsx";
    }
    auto mount_x = util::make_temp_dir().value();
    auto mount_y = util::make_temp_dir().value();
    // make mount_y sort after mount_x whatever names the temp dirs get
    if (mount_y < mount_x) {
        std::swap(mount_x, mount_y);
    }

    const auto shared = fmt::format("{}:{},{}:{},{}:{}", sqfs_3, mount_y,
                                    sqfs_2, mount_x, sqfs_1, mount_x);

    SECTION("rejected by default") {
        auto result = uenv::parse_and_validate_mounts(shared);
        REQUIRE(!result);
        REQUIRE_THAT(result.error(), Catch::Matchers::ContainsSubstring(
                                         "more than one squashfs"));
    }

    SECTION("allowed on request, keeping the order given") {
        auto result = uenv::parse_and_validate_mounts(shared, true);
        REQUIRE(result);
        REQUIRE(result->size() == 3);
        // sorted by mount point; images that share one keep their order
        REQUIRE(result->at(0).sqfs == sqfs_2);
        REQUIRE(result->at(0).mount == mount_x);
        REQUIRE(result->at(1).sqfs == sqfs_1);
        REQUIRE(result->at(1).mount == mount_x);
        REQUIRE(result->at(2).sqfs == sqfs_3);
        REQUIRE(result->at(2).mount == mount_y);
    }

    SECTION("a shared mount point must still exist") {
        auto missing = mount_x / "missing";
        auto result = uenv::parse_and_validate_mounts(
            fmt::format("{}:{},{}:{}", sqfs_1, missing, sqfs_2, missing), true);
        REQUIRE(!result);
        REQUIRE_THAT(result.error(),
                     Catch::Matchers::ContainsSubstring("does not exist"));
    }
}

TEST_CASE("validate_mounts error messages", "[mount]") {
    namespace fs = std::filesystem;

    auto root = util::make_temp_dir().value();
    auto sqfs = root / "image.squashfs";
    std::ofstream{sqfs} << "hsqsx";
    auto mount = util::make_temp_dir().value();

    auto validate = [&sqfs](const fs::path& m) {
        return uenv::parse_and_validate_mounts(fmt::format("{}:{}", sqfs, m));
    };

    // fs::status sets its error code for a path that does not exist: that
    // must be reported as a missing path, not as a path that can't be examined
    SECTION("missing mount point") {
        auto result = validate(mount / "missing");
        REQUIRE(!result);
        REQUIRE_THAT(result.error(),
                     Catch::Matchers::ContainsSubstring("does not exist"));
        REQUIRE_THAT(result.error(),
                     !Catch::Matchers::ContainsSubstring("unable to access"));
    }

    SECTION("mount point is a file") {
        auto result = validate(sqfs);
        REQUIRE(!result);
        REQUIRE_THAT(result.error(),
                     Catch::Matchers::ContainsSubstring("is not a directory"));
    }

    // a mount point that can't be examined must be an error, not an
    // exception, and the error must give the reason. (It is caught when the
    // path is resolved, before validate_mount_list examines it. A path
    // component longer than NAME_MAX can't be used here: the parser of the
    // mount list rejects it before the file system is touched.)
    SECTION("mount point that can't be examined") {
        if (geteuid() == 0) {
            SKIP("root ignores file permissions");
        }
        const auto locked = mount / "locked";
        fs::create_directories(locked / "mnt");
        fs::permissions(locked, fs::perms::none);
        auto restore = util::defer(
            [&locked]() { fs::permissions(locked, fs::perms::owner_all); });

        std::optional<decltype(validate(mount))> result;
        REQUIRE_NOTHROW(result = validate(locked / "mnt"));
        REQUIRE(!*result);
        REQUIRE_THAT((*result).error(),
                     Catch::Matchers::ContainsSubstring("Permission denied"));
    }
}
