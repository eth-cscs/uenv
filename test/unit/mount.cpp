#include <filesystem>
#include <fstream>

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

TEST_CASE("is_nested_path", "[mount]") {
    using uenv::is_nested_path;

    // strict child
    REQUIRE(is_nested_path("/data", "/data/foo"));
    // deep-nested child
    REQUIRE(is_nested_path("/data", "/data/foo/bar"));
    // parent == child is treated as nested
    REQUIRE(is_nested_path("/data", "/data"));
    // sibling with a shared string prefix, not nested
    REQUIRE(!is_nested_path("/data", "/data2"));
    // unrelated paths
    REQUIRE(!is_nested_path("/data/foo", "/data"));
    REQUIRE(!is_nested_path("/a/b", "/c/d"));
}

TEST_CASE("validate_sqfs_not_shadowed", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_pair;
    using uenv::tmpfs_tuple;

    const uenv::mount_list mounts = {
        mount_pair{.sqfs = "/img/app.squashfs", .mount = "/user-environment"},
    };

    // a --tmpfs nested under an --sqfs mount point can never be made visible
    {
        const std::vector<tmpfs_tuple> tmpfs = {
            tmpfs_tuple{.mount = "/user-environment/scratch",
                       .size = std::nullopt},
        };
        REQUIRE(!uenv::validate_sqfs_not_shadowed(mounts, {}, tmpfs));
    }

    // a --bind-mount destination exactly equal to an --sqfs mount point is
    // the same problem
    {
        const std::vector<bindmount_pair> bind_mounts = {
            bindmount_pair{.src = "/host/data", .dst = "/user-environment"},
        };
        REQUIRE(!uenv::validate_sqfs_not_shadowed(mounts, bind_mounts, {}));
    }

    // the reverse nesting -- --sqfs under a --tmpfs destination -- is fine,
    // because the destination always mounts before the later --sqfs mount
    {
        const std::vector<tmpfs_tuple> tmpfs = {
            tmpfs_tuple{.mount = "/data", .size = std::nullopt},
        };
        const uenv::mount_list nested_mounts = {
            mount_pair{.sqfs = "/img/app.squashfs", .mount = "/data/foo"},
        };
        REQUIRE(uenv::validate_sqfs_not_shadowed(nested_mounts, {}, tmpfs));
    }

    // unrelated paths
    {
        const std::vector<bindmount_pair> bind_mounts = {
            bindmount_pair{.src = "/host/data", .dst = "/other"},
        };
        const std::vector<tmpfs_tuple> tmpfs = {
            tmpfs_tuple{.mount = "/scratch", .size = std::nullopt},
        };
        REQUIRE(uenv::validate_sqfs_not_shadowed(mounts, bind_mounts, tmpfs));
    }

    // empty bind_mounts/tmpfs is always fine, regardless of mounts -- this is
    // the shape the Slurm plugin call site always passes
    REQUIRE(uenv::validate_sqfs_not_shadowed(mounts, {}, {}));
}

TEST_CASE("build_mount_order", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_kind;
    using uenv::mount_order_entry;
    using uenv::tmpfs_tuple;

    auto require_order = [](const uenv::mount_order& order,
                            const std::vector<mount_order_entry>& expected) {
        REQUIRE(order.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            REQUIRE(order[i].kind == expected[i].kind);
            REQUIRE(order[i].index == expected[i].index);
        }
    };

    // unrelated destinations sort alphabetically by destination path,
    // regardless of which vector they came from or what order they were
    // given in
    {
        const std::vector<bindmount_pair> bind_mounts = {
            {"/src", "/c"}, {"/src", "/a"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/b", std::nullopt}};
        auto order = uenv::build_mount_order(bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 1}, // /a
                                  {mount_kind::tmpfs, 0},      // /b
                                  {mount_kind::bind_mount, 0}, // /c
                              });
    }

    // a parent/child pair always orders the parent first, independent of
    // which is bind-mount vs tmpfs and independent of input order: a parent
    // directory's path is always a string prefix of anything nested under it
    {
        const std::vector<bindmount_pair> bind_mounts = {
            {"/src", "/parent/child"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/parent", std::nullopt}};
        auto order = uenv::build_mount_order(bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::tmpfs, 0},      // /parent
                                  {mount_kind::bind_mount, 0}, // /parent/child
                              });
    }
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/src", "/parent"}};
        const std::vector<tmpfs_tuple> tmpfs = {
            {"/parent/child", std::nullopt}};
        auto order = uenv::build_mount_order(bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 0}, // /parent
                                  {mount_kind::tmpfs, 0},      // /parent/child
                              });
    }

    // an identical destination used by both a --bind-mount and a --tmpfs is
    // rejected, rather than picking a winner
    {
        const std::vector<bindmount_pair> bind_mounts = {
            {"/src", "/same"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/same", std::nullopt}};
        REQUIRE(!uenv::build_mount_order(bind_mounts, tmpfs));
    }

    // an identical destination used twice within the same vector is also
    // rejected
    {
        const std::vector<bindmount_pair> bind_mounts = {
            {"/src1", "/same"}, {"/src2", "/same"}};
        const std::vector<tmpfs_tuple> tmpfs = {};
        REQUIRE(!uenv::build_mount_order(bind_mounts, tmpfs));
    }
}
