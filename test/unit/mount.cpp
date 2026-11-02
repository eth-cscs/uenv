#include <filesystem>
#include <fstream>
#include <optional>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>
#include <fmt/std.h>

#include <uenv/mount.h>
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

    // a path component longer than NAME_MAX can't be examined: this must be
    // an error, not an exception
    SECTION("mount point that can't be examined") {
        std::optional<decltype(validate(mount))> result;
        REQUIRE_NOTHROW(result = validate(mount / std::string(300, 'a')));
        REQUIRE(!*result);
    }
}
