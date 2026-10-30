#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <unistd.h>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
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

// A --bind-mount source is always the host's directory, so it never changes
// the order: only the nesting of the destinations does.
TEST_CASE("build_mount_order: bind-mount sources do not change the order",
          "[mount]") {
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

    // a source inside a squashfs mount point
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/user-environment"},
        };
        const std::vector<bindmount_pair> bind_mounts = {
            {"/user-environment/env", "/opt/env"}};
        auto order = uenv::build_mount_order(mounts, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 0}, // /opt/env
                                  {mount_kind::sqfs, 0}, // /user-environment
                              });
    }

    // a source inside a --tmpfs
    {
        const std::vector<bindmount_pair> bind_mounts = {
            {"/scratch/data", "/a"}};
        const std::vector<tmpfs_tuple> tmpfs = {{"/scratch", std::nullopt}};
        auto order = uenv::build_mount_order({}, bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 0}, // /a
                                  {mount_kind::tmpfs, 0},      // /scratch
                              });
    }

    // sources inside each other's destination are not a cycle
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/b/x", "/a"},
                                                         {"/a/y", "/b"}};
        auto order = uenv::build_mount_order({}, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 0}, // /a
                                  {mount_kind::bind_mount, 1}, // /b
                              });
    }

    // an image inside a bind destination, whose source is inside the image's
    // mount point
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img/a.squashfs", .mount = "/opt/env"},
        };
        const std::vector<bindmount_pair> bind_mounts = {
            {"/opt/env/x", "/opt"}};
        auto order = uenv::build_mount_order(mounts, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 0}, // /opt
                                  {mount_kind::sqfs, 0},       // /opt/env
                              });
    }

    // a source inside its own destination
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/d/sub", "/d"}};
        REQUIRE(uenv::build_mount_order({}, bind_mounts, {}));
    }
}

namespace {

// The inputs of build_mount_order, for the tests below.
struct mount_input {
    uenv::mount_list mounts;
    std::vector<uenv::bindmount_pair> bind_mounts;
    std::vector<uenv::tmpfs_tuple> tmpfs;

    uenv::mount_order all_entries() const {
        uenv::mount_order result;
        for (std::size_t i = 0; i < mounts.size(); ++i) {
            result.push_back({uenv::mount_kind::sqfs, i});
        }
        for (std::size_t i = 0; i < bind_mounts.size(); ++i) {
            result.push_back({uenv::mount_kind::bind_mount, i});
        }
        for (std::size_t i = 0; i < tmpfs.size(); ++i) {
            result.push_back({uenv::mount_kind::tmpfs, i});
        }
        return result;
    }

    const std::filesystem::path& dst(const uenv::mount_order_entry& e) const {
        return uenv::mount_destination(e, mounts, bind_mounts, tmpfs);
    }

    util::expected<uenv::mount_order, std::string> order() const {
        return uenv::build_mount_order(mounts, bind_mounts, tmpfs);
    }
};

bool same_entry(const uenv::mount_order_entry& a,
                const uenv::mount_order_entry& b) {
    return a.kind == b.kind && a.index == b.index;
}

// The first constraint `order` violates, or an empty string. Worked out from
// the inputs alone, not with the implementation's helpers:
// - every entry appears exactly once;
// - an entry comes after every destination its own destination lies in.
std::string check_order(const mount_input& in, const uenv::mount_order& order) {
    const auto all = in.all_entries();
    if (order.size() != all.size()) {
        return fmt::format("{} entries, expected {}", order.size(), all.size());
    }
    auto position = [&](const uenv::mount_order_entry& e) {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (same_entry(order[i], e)) {
                return i;
            }
        }
        return order.size();
    };
    for (const auto& e : all) {
        if (position(e) == order.size()) {
            return fmt::format("{} is missing", in.dst(e));
        }
    }
    for (const auto& b : all) {
        for (const auto& a : all) {
            if (same_entry(a, b)) {
                continue;
            }
            const bool parent =
                in.dst(a) != in.dst(b) && util::is_child(in.dst(b), in.dst(a));
            if (parent && position(a) > position(b)) {
                return fmt::format("{} is mounted after {}", in.dst(a),
                                   in.dst(b));
            }
        }
    }
    return {};
}

std::vector<std::string> destinations(const mount_input& in,
                                      const uenv::mount_order& order) {
    std::vector<std::string> result;
    for (const auto& e : order) {
        result.push_back(in.dst(e).string());
    }
    return result;
}

} // namespace

// build_mount_order is a topological sort: each entry after the mount that
// provides its mount point, and otherwise in the lexical order of the
// destinations. A --bind-mount source plays no part.
TEST_CASE("build_mount_order: dependency graph", "[mount]") {
    using uenv::mount_pair;
    using V = std::vector<std::string>;

    // the order of a valid input, checked against every constraint
    auto ordered = [](const mount_input& in) {
        auto order = in.order();
        REQUIRE(order);
        INFO(fmt::format("{}", destinations(in, *order)));
        REQUIRE(check_order(in, *order) == "");
        return destinations(in, *order);
    };

    // a chain of binds, each source in the next one's destination: the
    // sources are on the host, so the order is lexical
    REQUIRE(ordered({.bind_mounts = {{"/d/x", "/c"},
                                     {"/c/x", "/b"},
                                     {"/b/x", "/a"},
                                     {"/host", "/d"}}}) ==
            V{"/a", "/b", "/c", "/d"});

    // two binds with sources in an image, and a tmpfs in one of them that
    // holds a third bind
    REQUIRE(ordered({.mounts = {mount_pair{.sqfs = "/img", .mount = "/u"}},
                     .bind_mounts = {{"/u/a", "/p"},
                                     {"/u/b", "/q"},
                                     {"/q/x", "/p/t/y"}},
                     .tmpfs = {{"/p/t", std::nullopt}}}) ==
            V{"/p", "/p/t", "/p/t/y", "/q", "/u"});

    // a mount point in a tmpfs that sorts after a sibling of the tmpfs
    REQUIRE(ordered({.mounts = {mount_pair{.sqfs = "/img", .mount = "/z"}},
                     .bind_mounts = {{"/z/x", "/a/b"}},
                     .tmpfs = {{"/a", std::nullopt}}}) ==
            V{"/a", "/a/b", "/z"});

    // what used to be cycles through a source: ordered by nesting alone
    REQUIRE(ordered({.bind_mounts = {{"/b/x", "/a"},
                                     {"/c/x", "/b"},
                                     {"/a/x", "/c"}}}) == V{"/a", "/b", "/c"});
    REQUIRE(ordered({.bind_mounts = {{"/t/in/x", "/t"}},
                     .tmpfs = {{"/t/in", std::nullopt}}}) == V{"/t", "/t/in"});
}

// Every input of up to three entries over a small tree of paths is ordered,
// and the order satisfies every constraint.
TEST_CASE("build_mount_order: generated inputs", "[mount]") {
    const std::vector<std::string> pool = {"/a", "/a/b", "/a/b/c",
                                           "/b", "/b/a", "/c"};
    // the choices for one entry: an image, a tmpfs, or a bind with a source
    // from the pool or the host
    std::vector<std::string> sources = pool;
    sources.push_back("/host");
    const std::size_t nchoices = 2 + sources.size();

    std::size_t ninputs = 0;
    // every set of distinct destinations, as a bit mask over the pool
    for (unsigned mask = 1; mask < (1u << pool.size()); ++mask) {
        std::vector<std::string> dsts;
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (mask & (1u << i)) {
                dsts.push_back(pool[i]);
            }
        }
        if (dsts.size() > 3) {
            continue;
        }
        std::size_t ncombinations = 1;
        for (std::size_t i = 0; i < dsts.size(); ++i) {
            ncombinations *= nchoices;
        }
        for (std::size_t c = 0; c < ncombinations; ++c) {
            mount_input in;
            auto rest = c;
            for (const auto& d : dsts) {
                const auto choice = rest % nchoices;
                rest /= nchoices;
                if (choice == 0) {
                    in.mounts.push_back({.sqfs = "/img", .mount = d});
                } else if (choice == 1) {
                    in.tmpfs.push_back({d, std::nullopt});
                } else {
                    in.bind_mounts.push_back({sources[choice - 2], d});
                }
            }
            ++ninputs;
            auto order = in.order();
            REQUIRE(order);
            INFO(fmt::format("{}", destinations(in, *order)));
            REQUIRE(check_order(in, *order) == "");
        }
    }
    REQUIRE(ninputs > 10000);
}

// parse_and_canonical_tmpfs_list and parse_and_canonical_bindmount_list put
// paths in the same canonical form as --sqfs mount points
// (uenv::canonical_mount_path): absolute, lexically normal and without a
// trailing separator, so that the mount order and the duplicate check
// (uenv::build_mount_order) compare like with like.
TEST_CASE("canonical tmpfs and bind-mount lists", "[mount]") {
    namespace fs = std::filesystem;
    auto tmpdir = fs::weakly_canonical(fs::temp_directory_path() /
                                       "uenv-parse-normalise-test");
    fs::create_directories(tmpdir / "sub");

    // a trailing separator is dropped
    {
        auto result =
            uenv::parse_and_canonical_tmpfs_list({tmpdir.string() + "/"});
        REQUIRE(result);
        REQUIRE((*result)[0].mount == tmpdir);
    }
    {
        auto result = uenv::parse_and_canonical_bindmount_list(
            {"/host/dir/:" + tmpdir.string() + "/"});
        REQUIRE(result);
        REQUIRE((*result)[0].src == "/host/dir");
        REQUIRE((*result)[0].dst == tmpdir);
    }
    // also after a part of the path that does not exist
    {
        auto result = uenv::parse_and_canonical_tmpfs_list(
            {tmpdir.string() + "/missing/deeper/"});
        REQUIRE(result);
        REQUIRE((*result)[0].mount == tmpdir / "missing" / "deeper");
    }
    // "." and ".." components are resolved
    {
        auto result = uenv::parse_and_canonical_tmpfs_list(
            {tmpdir.string() + "/./sub/.."});
        REQUIRE(result);
        REQUIRE((*result)[0].mount == tmpdir);
    }
    {
        auto result = uenv::parse_and_canonical_bindmount_list(
            {"/host/dir:" + tmpdir.string() + "/sub/../sub/."});
        REQUIRE(result);
        REQUIRE((*result)[0].dst == tmpdir / "sub");
    }
    // a path relative to the working directory is made absolute, as a
    // --sqfs mount point is
    {
        auto cwd = fs::current_path();
        fs::current_path(tmpdir);
        auto tmpfs = uenv::parse_and_canonical_tmpfs_list({"./sub"});
        auto binds = uenv::parse_and_canonical_bindmount_list({"./sub:."});
        fs::current_path(cwd);
        REQUIRE(tmpfs);
        REQUIRE((*tmpfs)[0].mount == tmpdir / "sub");
        REQUIRE(binds);
        REQUIRE((*binds)[0].src == tmpdir / "sub");
        REQUIRE((*binds)[0].dst == tmpdir);
    }

    fs::remove_all(tmpdir);
}

// Destinations are compared as written: the order and the duplicate check
// only work if every destination is spelled the same way, i.e. absolute,
// lexically normal and without a trailing separator. These go through
// parse_and_canonical_tmpfs_list and parse_and_canonical_bindmount_list, which
// is where that spelling is fixed.
TEST_CASE("build_mount_order: normalised destinations", "[mount]") {
    using uenv::mount_kind;

    auto base =
        std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "a" / "b");
    std::filesystem::create_directories(base / "z");
    auto sqfs = base / "img.squashfs";
    std::ofstream{sqfs} << "hsqsx";

    // a trailing separator: "<base>/a/" and "<base>/a" are the same mount
    // point, so using both is a duplicate
    {
        auto tmpfs = uenv::parse_and_canonical_tmpfs_list(
            fmt::format("{}/", (base / "a").string()));
        REQUIRE(tmpfs);
        auto binds = uenv::parse_and_canonical_bindmount_list(
            fmt::format("/host:{}", (base / "a").string()));
        REQUIRE(binds);
        REQUIRE(!uenv::build_mount_order({}, *binds, *tmpfs));
    }

    // "." and ".." components: "<base>/a/./b" and "<base>/a/b" are the same
    {
        auto tmpfs = uenv::parse_and_canonical_tmpfs_list(
            fmt::format("{}/a/./b", base.string()));
        REQUIRE(tmpfs);
        auto binds = uenv::parse_and_canonical_bindmount_list(
            fmt::format("/host:{}/z/../a/b", base.string()));
        REQUIRE(binds);
        REQUIRE(!uenv::build_mount_order({}, *binds, *tmpfs));
    }

    // "<base>/z/../a" is the parent of "<base>/a/b", although it sorts after
    // it as written
    {
        auto tmpfs = uenv::parse_and_canonical_tmpfs_list(
            fmt::format("{}/z/../a", base.string()));
        REQUIRE(tmpfs);
        auto binds = uenv::parse_and_canonical_bindmount_list(
            fmt::format("/host:{}/a/b", base.string()));
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
            uenv::parse_and_canonical_tmpfs_list((base / "a").string());
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
        auto tmpfs = uenv::parse_and_canonical_tmpfs_list(
            (base / "real" / "a").string());
        REQUIRE(tmpfs);
        REQUIRE(!uenv::build_mount_order(*mounts, {}, *tmpfs));

        // and the other way round
        mounts = uenv::parse_and_validate_mounts(fmt::format(
            "{}:{}", sqfs.string(), (base / "real" / "a").string()));
        REQUIRE(mounts);
        tmpfs = uenv::parse_and_canonical_tmpfs_list(
            (base / "link" / "a").string());
        REQUIRE(tmpfs);
        REQUIRE(!uenv::build_mount_order(*mounts, {}, *tmpfs));
    }

    std::filesystem::remove_all(base);
}

TEST_CASE("mount_point_providers", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_kind;
    using uenv::mount_pair;
    using uenv::tmpfs_tuple;

    // the provider of each entry, in mount order
    auto providers = [](const uenv::mount_list& mounts,
                        const std::vector<bindmount_pair>& bind_mounts,
                        const std::vector<tmpfs_tuple>& tmpfs) {
        auto order = uenv::build_mount_order(mounts, bind_mounts, tmpfs);
        REQUIRE(order);
        return uenv::mount_point_providers(mounts, bind_mounts, tmpfs, *order);
    };
    using P = std::vector<std::optional<mount_kind>>;

    // nothing nested: every mount point is on the host
    REQUIRE(providers({}, {{"/src", "/a"}}, {{"/b", std::nullopt}}) ==
            P{std::nullopt, std::nullopt});

    // a chain: tmpfs on the host, an image in the tmpfs, a bind-mount in the
    // image
    REQUIRE(providers({mount_pair{.sqfs = "/img.squashfs", .mount = "/t/s"}},
                      {{"/src", "/t/s/x"}}, {{"/t", std::nullopt}}) ==
            P{std::nullopt, mount_kind::tmpfs, mount_kind::sqfs});

    // the innermost provider wins: /t/s/x is in the image, not in the tmpfs
    REQUIRE(providers({mount_pair{.sqfs = "/img.squashfs", .mount = "/t/s"}},
                      {}, {{"/t", std::nullopt}, {"/t/s/x", std::nullopt}}) ==
            P{std::nullopt, mount_kind::tmpfs, mount_kind::sqfs});

    // a mount point in a bind-mount
    REQUIRE(providers({}, {{"/src", "/b"}}, {{"/b/inner", std::nullopt}}) ==
            P{std::nullopt, mount_kind::bind_mount});

    // a sibling with a shared string prefix is not a provider
    REQUIRE(providers({}, {{"/src", "/data2"}}, {{"/data", std::nullopt}}) ==
            P{std::nullopt, std::nullopt});

    // a bind source inside an image does not change the order or the
    // providers: /b and the tmpfs inside it, then /z and the image inside it
    REQUIRE(providers({mount_pair{.sqfs = "/img.squashfs", .mount = "/z/s"}},
                      {{"/z/s/env", "/b"}},
                      {{"/z", std::nullopt}, {"/b/inner", std::nullopt}}) ==
            P{std::nullopt, mount_kind::bind_mount, std::nullopt,
              mount_kind::tmpfs});
}

TEST_CASE("validate_mount_points", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_pair;
    using uenv::tmpfs_tuple;

    auto base =
        std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "exists");
    const auto exists = base / "exists";
    const auto missing = base / "missing";
    // a bind-mount source that exists, with the mount point that the first
    // test below puts inside a bind of it onto `base`
    const auto src = base / "src";
    std::filesystem::create_directories(src / "exists");

    for (bool mutable_root : {false, true}) {
        // mount points that exist on the host
        REQUIRE(uenv::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = exists}},
            {{src, base}}, {}, mutable_root));
        REQUIRE(uenv::validate_mount_points(
            {}, {{src, exists}}, {{base, std::nullopt}}, mutable_root));
    }

    // a mount point missing on the host: an error, unless a mutable root
    // creates it (also under a writable directory, see writable_shadowed_dirs)
    {
        const std::vector<tmpfs_tuple> tmpfs = {{missing, std::nullopt}};
        REQUIRE(!uenv::validate_mount_points({}, {}, tmpfs, false));
        REQUIRE(uenv::validate_mount_points({}, {}, tmpfs, true));
    }
    {
        const std::vector<bindmount_pair> binds = {{src, missing}};
        REQUIRE(!uenv::validate_mount_points({}, binds, {}, false));
        REQUIRE(uenv::validate_mount_points({}, binds, {}, true));
    }
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img.squashfs", .mount = missing}};
        REQUIRE(!uenv::validate_mount_points(mounts, {}, {}, false));
        REQUIRE(uenv::validate_mount_points(mounts, {}, {}, true));
    }

    // with a mutable root, a missing mount point is accepted whatever its
    // deepest existing ancestor, a read-only directory or "/"
    {
        REQUIRE(uenv::validate_mount_points(
            {}, {}, {{"/uenv-unit-test-missing/a", std::nullopt}}, true));
        if (::geteuid() != 0) {
            const auto ro = base / "read-only";
            std::filesystem::create_directories(ro);
            std::filesystem::permissions(
                ro, std::filesystem::perms::owner_read |
                        std::filesystem::perms::owner_exec);
            REQUIRE(uenv::validate_mount_points(
                {}, {}, {{ro / "new/deeper", std::nullopt}}, true));
            REQUIRE(!uenv::validate_mount_points(
                {}, {}, {{ro / "new/deeper", std::nullopt}}, false));
            std::filesystem::permissions(ro, std::filesystem::perms::owner_all);
        }
        // a file in the path can never become a directory
        std::ofstream(base / "file").close();
        REQUIRE(!uenv::validate_mount_points(
            {}, {}, {{base / "file/new", std::nullopt}}, true));
    }

    // a mount point provided by another mount is not checked with a mutable
    // root either
    REQUIRE(uenv::validate_mount_points(
        {}, {{src, exists / "new/x"}},
        {{exists, std::nullopt}, {exists / "new", std::nullopt}}, true));

    // a missing mount point inside a --tmpfs or --sqfs is provided by that
    // mount, and is not checked on the host
    {
        const std::vector<tmpfs_tuple> tmpfs = {{exists, std::nullopt}};
        REQUIRE(uenv::validate_mount_points({}, {{src, exists / "new"}}, tmpfs,
                                            false));
        REQUIRE(uenv::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = exists / "new"}}, {},
            tmpfs, false));
        REQUIRE(uenv::validate_mount_points(
            {}, {}, {{exists, std::nullopt}, {exists / "new", std::nullopt}},
            false));
    }
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img.squashfs", .mount = exists}};
        REQUIRE(uenv::validate_mount_points(
            mounts, {}, {{exists / "in-image", std::nullopt}}, false));
    }

    // a mount point inside a --bind-mount is the path below its source on
    // the host, so it is checked, and never created: also with a mutable root
    for (bool mutable_root : {false, true}) {
        std::filesystem::remove_all(src / "in-source");
        auto r = uenv::validate_mount_points(
            {}, {{src, exists}}, {{exists / "in-source", std::nullopt}},
            mutable_root);
        REQUIRE(!r);
        REQUIRE(r.error() ==
                fmt::format("the mount point {} does not exist: it is inside "
                            "the --bind-mount {}:{}, where {} is not a "
                            "directory",
                            (exists / "in-source").string(), src.string(),
                            exists.string(), (src / "in-source").string()));
        std::filesystem::create_directories(src / "in-source");
        REQUIRE(uenv::validate_mount_points(
            {}, {{src, exists}}, {{exists / "in-source", std::nullopt}},
            mutable_root));
        // only the innermost provider counts: a --tmpfs inside the bind
        // provides what is below it
        REQUIRE(uenv::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs",
                        .mount = exists / "in-source/new"}},
            {{src, exists}}, {{exists / "in-source", std::nullopt}},
            mutable_root));
        // and the provider is the bind, not the host path it covers
        std::filesystem::create_directories(exists / "host-only");
        REQUIRE(!uenv::validate_mount_points(
            {}, {{src, exists}}, {{exists / "host-only", std::nullopt}},
            mutable_root));
        std::filesystem::remove_all(src / "in-source");
    }

    // but the provider itself has to exist
    REQUIRE(!uenv::validate_mount_points({}, {{src, missing / "new"}},
                                         {{missing, std::nullopt}}, false));

    // a bind-mount source on the host must be an existing directory, with or
    // without a mutable root
    for (bool mutable_root : {false, true}) {
        REQUIRE(!uenv::validate_mount_points({}, {{missing, exists}}, {},
                                             mutable_root));
        std::ofstream(base / "src-file").close();
        REQUIRE(!uenv::validate_mount_points({}, {{base / "src-file", exists}},
                                             {}, mutable_root));
        // inside another destination: still the host's directory, checked
        REQUIRE(!uenv::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = exists}},
            {{exists / "in-image", src}}, {}, mutable_root));
        REQUIRE(!uenv::validate_mount_points({}, {{exists / "in-tmpfs", src}},
                                             {{exists, std::nullopt}},
                                             mutable_root));
        std::filesystem::create_directories(exists / "on-host");
        REQUIRE(uenv::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = exists}},
            {{exists / "on-host", src}}, {}, mutable_root));
        REQUIRE(uenv::validate_mount_points({}, {{exists / "on-host", src}},
                                            {{exists, std::nullopt}},
                                            mutable_root));
        // inside its own destination: the host path, checked
        REQUIRE(!uenv::validate_mount_points({}, {{exists / "missing", exists}},
                                             {}, mutable_root));
    }
    {
        auto r = uenv::validate_mount_points({}, {{missing, exists}}, {}, true);
        REQUIRE(!r);
        REQUIRE(r.error() ==
                fmt::format("the bind-mount source {} does not exist",
                            missing.string()));
    }

    std::filesystem::remove_all(base);
}

TEST_CASE("writable_shadowed_dirs", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_pair;
    using P =
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>>;

    auto base =
        std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "exists");

    // a missing mount point under a writable directory, several levels deep:
    // reported with its deepest existing ancestor
    REQUIRE(
        uenv::writable_shadowed_dirs({}, {}, {{base / "new", std::nullopt}}) ==
        P{{base / "new", base}});
    REQUIRE(uenv::writable_shadowed_dirs(
                {mount_pair{.sqfs = "/img", .mount = base / "exists/a/b"}}, {},
                {}) == P{{base / "exists/a/b", base / "exists"}});

    // existing, below "/" only, or provided by another mount: nothing
    REQUIRE(uenv::writable_shadowed_dirs({}, {{"/src", base / "exists"}}, {})
                .empty());
    REQUIRE(uenv::writable_shadowed_dirs(
                {}, {}, {{"/uenv-unit-test-missing/x", std::nullopt}})
                .empty());
    REQUIRE(uenv::writable_shadowed_dirs({}, {{"/src", base / "exists/new"}},
                                         {{base / "exists", std::nullopt}})
                .empty());

    // a directory the caller can not write to
    if (::geteuid() != 0) {
        const auto ro = base / "read-only";
        std::filesystem::create_directories(ro);
        std::filesystem::permissions(ro,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_exec);
        REQUIRE(
            uenv::writable_shadowed_dirs({}, {}, {{ro / "new", std::nullopt}})
                .empty());
        std::filesystem::permissions(ro, std::filesystem::perms::owner_all);
    }

    std::filesystem::remove_all(base);
}

TEST_CASE("deepest_existing_ancestor", "[mount]") {
    auto base =
        std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "a/b");
    std::ofstream(base / "a/file").close();
    std::filesystem::create_symlink("does-not-exist", base / "a/dangling");

    REQUIRE(uenv::deepest_existing_ancestor(base / "a/b").value() ==
            base / "a/b");
    REQUIRE(uenv::deepest_existing_ancestor(base / "a/b/c").value() ==
            base / "a/b");
    REQUIRE(uenv::deepest_existing_ancestor(base / "a/x/y/z").value() ==
            base / "a");
    REQUIRE(
        uenv::deepest_existing_ancestor("/uenv-unit-test-missing/x").value() ==
        "/");
    REQUIRE(uenv::deepest_existing_ancestor("/").value() == "/");

    // a file or a dangling symlink in the path
    REQUIRE(!uenv::deepest_existing_ancestor(base / "a/file/x"));
    REQUIRE(!uenv::deepest_existing_ancestor(base / "a/dangling/x"));

    std::filesystem::remove_all(base);
}

TEST_CASE("mutable_root_shadow_dirs", "[mount]") {
    using S = std::set<std::filesystem::path>;
    auto base =
        std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "a/b");
    std::filesystem::create_directories(base / "c");
    std::ofstream(base / "file").close();

    // "/" and every ancestor of base
    S base_path;
    for (auto p = base; p != p.root_path(); p = p.parent_path()) {
        base_path.insert(p);
    }
    auto with_base = [&](S s) {
        s.insert(base_path.begin(), base_path.end());
        s.insert("/");
        return s;
    };

    // nothing to create: only "/"
    REQUIRE(uenv::mutable_root_shadow_dirs({}).value() == S{"/"});
    REQUIRE(
        uenv::mutable_root_shadow_dirs({base / "a/b", base / "c"}).value() ==
        S{"/"});

    // a new top-level name only needs "/"
    REQUIRE(
        uenv::mutable_root_shadow_dirs({"/uenv-unit-test-missing/x"}).value() ==
        S{"/"});

    // a missing leaf: the path down to its parent
    REQUIRE(uenv::mutable_root_shadow_dirs({base / "a/b/new"}).value() ==
            with_base({base / "a", base / "a/b"}));

    // missing several levels deep: down to the deepest existing ancestor
    REQUIRE(uenv::mutable_root_shadow_dirs({base / "a/x/y"}).value() ==
            with_base({base / "a"}));

    // several mount points: the union, existing ones add nothing
    REQUIRE(uenv::mutable_root_shadow_dirs(
                {base / "a/b/new", base / "c/new", base / "c"})
                .value() == with_base({base / "a", base / "a/b", base / "c"}));

    // a file in the path
    REQUIRE(!uenv::mutable_root_shadow_dirs({base / "file/new"}));

    std::filesystem::remove_all(base);
}
