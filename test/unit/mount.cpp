#include <filesystem>
#include <fstream>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>
#include <fmt/std.h>

#include <uenv/mount.h>
#include <uenv/parse.h>
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

TEST_CASE("build_mount_order", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_kind;
    using uenv::mount_order_entry;
    using uenv::mount_pair;
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
        const std::vector<bindmount_pair> bind_mounts = {{"/src", "/c"},
                                                         {"/src", "/a"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/b", std::nullopt}};
        auto order = uenv::build_mount_order({}, bind_mounts, tmpfs);
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
        auto order = uenv::build_mount_order({}, bind_mounts, tmpfs);
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
        auto order = uenv::build_mount_order({}, bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 0}, // /parent
                                  {mount_kind::tmpfs, 0},      // /parent/child
                              });
    }

    // an identical destination used by both a --bind-mount and a --tmpfs is
    // rejected, rather than picking a winner
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/src", "/same"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/same", std::nullopt}};
        REQUIRE(!uenv::build_mount_order({}, bind_mounts, tmpfs));
    }

    // an identical destination used twice within the same vector is also
    // rejected
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/src1", "/same"},
                                                         {"/src2", "/same"}};
        const std::vector<tmpfs_tuple> tmpfs = {};
        REQUIRE(!uenv::build_mount_order({}, bind_mounts, tmpfs));
    }

    // --sqfs takes part in the same order: a --tmpfs or --bind-mount inside
    // a squashfs image is mounted after the image, and an image inside a
    // --tmpfs is mounted after the tmpfs
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/b.squashfs", .mount = "/data/env"},
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/user-environment"},
        };
        const std::vector<bindmount_pair> bind_mounts = {
            {"/src", "/user-environment/linux"}};
        const std::vector<tmpfs_tuple> tmpfs = {
            {"/user-environment/scratch", std::nullopt},
            {"/data", std::nullopt}};
        auto order = uenv::build_mount_order(mounts, bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(
            *order, {
                        {mount_kind::tmpfs, 1},      // /data
                        {mount_kind::sqfs, 0},       // /data/env
                        {mount_kind::sqfs, 1},       // /user-environment
                        {mount_kind::bind_mount, 0}, // /user-environment/linux
                        {mount_kind::tmpfs, 0}, // /user-environment/scratch
                    });
    }

    // only squashfs images, the shape the Slurm plugin passes
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/b.squashfs", .mount = "/a/b"},
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/a"},
        };
        auto order = uenv::build_mount_order(mounts, {}, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::sqfs, 1}, // /a
                                  {mount_kind::sqfs, 0}, // /a/b
                              });
    }

    // an identical destination used by a --sqfs and a --tmpfs is rejected
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/same"},
        };
        const std::vector<tmpfs_tuple> tmpfs = {{"/same", std::nullopt}};
        REQUIRE(!uenv::build_mount_order(mounts, {}, tmpfs));
    }
}

// A --bind-mount reads its source when it is mounted, so a source that lies
// inside the destination of another --sqfs/--bind-mount/--tmpfs only exists
// once that one is mounted: it has to be ordered after it, even when its own
// destination sorts first.
TEST_CASE("build_mount_order: bind-mount sources", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_kind;
    using uenv::mount_order_entry;
    using uenv::mount_pair;
    using uenv::tmpfs_tuple;

    auto require_order = [](const uenv::mount_order& order,
                            const std::vector<mount_order_entry>& expected) {
        REQUIRE(order.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            REQUIRE(order[i].kind == expected[i].kind);
            REQUIRE(order[i].index == expected[i].index);
        }
    };

    // the source is inside a squashfs image: /opt/env sorts before
    // /user-environment, but the image must be mounted first
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/user-environment"},
        };
        const std::vector<bindmount_pair> bind_mounts = {
            {"/user-environment/env", "/opt/env"}};
        auto order = uenv::build_mount_order(mounts, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::sqfs, 0},       // /user-environment
                                  {mount_kind::bind_mount, 0}, // /opt/env
                              });
    }

    // the source is the squashfs mount point itself
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/user-environment"},
        };
        const std::vector<bindmount_pair> bind_mounts = {
            {"/user-environment", "/alias"}};
        auto order = uenv::build_mount_order(mounts, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::sqfs, 0},       // /user-environment
                                  {mount_kind::bind_mount, 0}, // /alias
                              });
    }

    // the source is inside a --tmpfs
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/scratch/data", "/a"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/scratch", std::nullopt}};
        auto order = uenv::build_mount_order({}, bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::tmpfs, 0},      // /scratch
                                  {mount_kind::bind_mount, 0}, // /a
                              });
    }

    // the source is inside the destination of another --bind-mount
    {
        const std::vector<bindmount_pair> bind_mounts = {
            {"/data/sub", "/alpha"}, {"/host", "/data"}};
        auto order = uenv::build_mount_order({}, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 1}, // /data
                                  {mount_kind::bind_mount, 0}, // /alpha
                              });
    }

    // a source inside a destination that is itself nested: the whole chain
    // is respected, parents before children and sources before their users
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/ue/inner"},
        };
        const std::vector<bindmount_pair> bind_mounts = {
            {"/ue/inner/env", "/a"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/ue", std::nullopt}};
        auto order = uenv::build_mount_order(mounts, bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::tmpfs, 0},      // /ue
                                  {mount_kind::sqfs, 0},       // /ue/inner
                                  {mount_kind::bind_mount, 0}, // /a
                              });
    }

    // two bind-mounts whose sources are each inside the other's destination
    // can not be ordered, and are rejected
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/b/x", "/a"},
                                                         {"/a/y", "/b"}};
        REQUIRE(!uenv::build_mount_order({}, bind_mounts, {}));
    }

    // a source unrelated to every destination does not change the order
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/host/x", "/b"},
                                                         {"/host/y", "/a"}};
        auto order = uenv::build_mount_order({}, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 1}, // /a
                                  {mount_kind::bind_mount, 0}, // /b
                              });
    }
}

// Destinations are compared as written: the order and the duplicate check
// only work if every destination is spelled the same way, i.e. absolute,
// lexically normal and without a trailing separator. These go through the
// parsers, which is where that spelling is fixed.
TEST_CASE("build_mount_order: normalised destinations", "[mount]") {
    using uenv::mount_kind;

    auto base = std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "a" / "b");
    std::filesystem::create_directories(base / "z");
    auto sqfs = base / "img.squashfs";
    std::ofstream{sqfs} << "hsqsx";

    // a trailing separator: "<base>/a/" and "<base>/a" are the same mount
    // point, so using both is a duplicate
    {
        auto tmpfs = uenv::parse_tmpfs_and_validate(
            fmt::format("{}/", (base / "a").string()), false);
        REQUIRE(tmpfs);
        auto binds = uenv::parse_bindmounts_and_validate(
            fmt::format("/host:{}", (base / "a").string()), false);
        REQUIRE(binds);
        REQUIRE(!uenv::build_mount_order({}, *binds, *tmpfs));
    }

    // "." and ".." components: "<base>/a/./b" and "<base>/a/b" are the same
    {
        auto tmpfs = uenv::parse_tmpfs_and_validate(
            fmt::format("{}/a/./b", base.string()), false);
        REQUIRE(tmpfs);
        auto binds = uenv::parse_bindmounts_and_validate(
            fmt::format("/host:{}/z/../a/b", base.string()), false);
        REQUIRE(binds);
        REQUIRE(!uenv::build_mount_order({}, *binds, *tmpfs));
    }

    // "<base>/z/../a" is the parent of "<base>/a/b", although it sorts after
    // it as written
    {
        auto tmpfs = uenv::parse_tmpfs_and_validate(
            fmt::format("{}/z/../a", base.string()), false);
        REQUIRE(tmpfs);
        auto binds = uenv::parse_bindmounts_and_validate(
            fmt::format("/host:{}/a/b", base.string()), false);
        REQUIRE(binds);
        auto order = uenv::build_mount_order({}, *binds, *tmpfs);
        REQUIRE(order);
        REQUIRE(order->size() == 2);
        REQUIRE((*order)[0].kind == mount_kind::tmpfs);
        REQUIRE((*order)[1].kind == mount_kind::bind_mount);
    }

    // a --sqfs mount point is normalised the same way: "<base>/a/" duplicates
    // a --tmpfs at "<base>/a"
    {
        auto mounts = uenv::parse_and_validate_mounts(
            fmt::format("{}:{}/", sqfs.string(), (base / "a").string()));
        REQUIRE(mounts);
        REQUIRE(mounts->size() == 1);
        REQUIRE((*mounts)[0].mount == base / "a");
        auto tmpfs =
            uenv::parse_tmpfs_and_validate((base / "a").string(), false);
        REQUIRE(tmpfs);
        REQUIRE(!uenv::build_mount_order(*mounts, {}, *tmpfs));
    }

    // a symlinked spelling: "<base>/link/a" is "<base>/real/a" when
    // link -> real, for a --sqfs mount point and a --tmpfs alike
    {
        std::filesystem::create_directories(base / "real" / "a");
        std::filesystem::create_directory_symlink("real", base / "link");
        auto mounts = uenv::parse_and_validate_mounts(fmt::format(
            "{}:{}", sqfs.string(), (base / "link" / "a").string()));
        REQUIRE(mounts);
        auto tmpfs = uenv::parse_tmpfs_and_validate(
            (base / "real" / "a").string(), false);
        REQUIRE(tmpfs);
        REQUIRE(!uenv::build_mount_order(*mounts, {}, *tmpfs));

        // and the other way round
        mounts = uenv::parse_and_validate_mounts(fmt::format(
            "{}:{}", sqfs.string(), (base / "real" / "a").string()));
        REQUIRE(mounts);
        tmpfs = uenv::parse_tmpfs_and_validate(
            (base / "link" / "a").string(), false);
        REQUIRE(tmpfs);
        REQUIRE(!uenv::build_mount_order(*mounts, {}, *tmpfs));
    }

    std::filesystem::remove_all(base);
}
