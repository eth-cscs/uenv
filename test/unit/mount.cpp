#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include <uenv/mount.h>
#include <uenv/parse.h>
#include <util/defer.h>
#include <util/fs.h>

// the steps of uenv::make_mount_plan, which are not declared in mount.h
namespace uenv::impl {
util::expected<mount_order, std::string>
build_mount_order(const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_mount>& tmpfs);
util::expected<void, std::string>
validate_mount_points(const mount_list& mounts,
                      const std::vector<bindmount_pair>& bind_mounts,
                      const std::vector<tmpfs_mount>& tmpfs);
} // namespace uenv::impl

namespace {
// from the values of --sqfs, --bind-mount and --tmpfs to the mounts to perform
util::expected<uenv::mount_plan, std::string>
plan_mounts(const std::optional<std::string>& sqfs,
            const std::optional<std::string>& bind_mounts = {},
            const std::optional<std::string>& tmpfs = {}) {
    auto request = uenv::parse_mount_request(sqfs, bind_mounts, tmpfs);
    if (!request) {
        return util::unexpected{request.error()};
    }
    return uenv::make_mount_plan(request.value());
}
} // namespace

// open_squashfs is what every open of a user-named image goes through: the
// check in make_mount_plan, and the open that each backend mounts from.
TEST_CASE("open_squashfs", "[mount]") {
    using Catch::Matchers::ContainsSubstring;
    namespace fs = std::filesystem;

    const auto dir = util::make_temp_dir().value();
    const auto image = dir / "image.squashfs";
    std::ofstream{image} << "hsqsx";

    // an image, as a path and as a mount in a plan
    auto as_mount = [&dir](const fs::path& sqfs) {
        return plan_mounts(fmt::format("{}:{}", sqfs.string(), dir.string()));
    };

    SECTION("an image") {
        auto fd = uenv::open_squashfs(image);
        REQUIRE(fd);
        REQUIRE(uenv::check_squashfs_fd(fd->get()));
        // O_NONBLOCK is only for the open: the descriptor is a blocking one
        const int flags = fcntl(fd->get(), F_GETFL);
        REQUIRE(flags >= 0);
        REQUIRE((flags & O_NONBLOCK) == 0);
        // and is not inherited by the command that is exec'd
        REQUIRE((fcntl(fd->get(), F_GETFD) & FD_CLOEXEC) != 0);

        REQUIRE(as_mount(image));
    }

    SECTION("a file that does not exist") {
        auto fd = uenv::open_squashfs(dir / "absent.squashfs");
        REQUIRE(!fd);
        REQUIRE_THAT(fd.error(), ContainsSubstring("No such file"));
        REQUIRE(!as_mount(dir / "absent.squashfs"));
    }

    SECTION("a directory") {
        auto fd = uenv::open_squashfs(dir);
        REQUIRE(!fd);
        REQUIRE(fd.error() == "is not a regular file");

        auto plan = as_mount(dir);
        REQUIRE(!plan);
        REQUIRE_THAT(plan.error(), ContainsSubstring("is not a regular file"));
    }

    SECTION("a file that is shorter than the magic number") {
        const auto file = dir / "short.squashfs";
        std::ofstream{file} << "hsq";
        auto fd = uenv::open_squashfs(file);
        REQUIRE(!fd);
        REQUIRE(fd.error() == "not a valid squashfs file");
        REQUIRE(!as_mount(file));
    }

    SECTION("an empty file") {
        const auto file = dir / "empty.squashfs";
        std::ofstream{file};
        REQUIRE(!uenv::open_squashfs(file));
        REQUIRE(!as_mount(file));
    }

    SECTION("a file with the wrong magic number") {
        const auto file = dir / "other.squashfs";
        std::ofstream{file} << "sqshxxxxxxxx";
        auto fd = uenv::open_squashfs(file);
        REQUIRE(!fd);
        REQUIRE(fd.error() == "not a valid squashfs file");

        auto plan = as_mount(file);
        REQUIRE(!plan);
        REQUIRE_THAT(plan.error(),
                     ContainsSubstring("not a valid squashfs file"));
    }

    SECTION("a symlink to an image") {
        const auto link = dir / "link.squashfs";
        fs::create_symlink(image, link);
        REQUIRE(uenv::open_squashfs(link));
        // unless the caller asks for it not to be followed
        REQUIRE(!uenv::open_squashfs(link, O_NOFOLLOW));
        REQUIRE(uenv::open_squashfs(image, O_NOFOLLOW));
    }

    // Opening a FIFO for reading blocks until it has a writer. A user can put
    // a FIFO where an image was, so the open must not wait for one: on the
    // kernel backend it is made inside slurmstepd.
    SECTION("a FIFO is refused without blocking") {
        const auto fifo = dir / "fifo.squashfs";
        REQUIRE(mkfifo(fifo.c_str(), 0600) == 0);

        // an open that blocks would hang the tests: stop them instead
        alarm(30);
        auto disarm = util::defer([]() { alarm(0); });

        auto fd = uenv::open_squashfs(fifo);
        REQUIRE(!fd);
        REQUIRE(fd.error() == "is not a regular file");

        auto plan = as_mount(fifo);
        REQUIRE(!plan);
        REQUIRE_THAT(plan.error(), ContainsSubstring("is not a regular file"));
    }
}

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
        REQUIRE(plan_mounts(input));
    }
    // invalid inputs
    for (auto input : {
             // a single mount point that does not exist
             fmt::format("{}:{}", sqfs_1, mount_a),
             // two mount points: the second does not exist
             fmt::format("{}:{},{}:{}", sqfs_1, mount_other, sqfs_2, mount_a),
         }) {
        REQUIRE(!plan_mounts(input));
    }
}

TEST_CASE("build_mount_order", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_kind;
    using uenv::mount_order_entry;
    using uenv::mount_pair;
    using uenv::tmpfs_mount;

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
        const std::vector<tmpfs_mount> tmpfs = {{"/b", std::nullopt}};
        auto order = uenv::impl::build_mount_order({}, bind_mounts, tmpfs);
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
        const std::vector<tmpfs_mount> tmpfs = {{"/parent", std::nullopt}};
        auto order = uenv::impl::build_mount_order({}, bind_mounts, tmpfs);
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::tmpfs, 0},      // /parent
                                  {mount_kind::bind_mount, 0}, // /parent/child
                              });
    }
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/src", "/parent"}};
        const std::vector<tmpfs_mount> tmpfs = {
            {"/parent/child", std::nullopt}};
        auto order = uenv::impl::build_mount_order({}, bind_mounts, tmpfs);
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
        const std::vector<tmpfs_mount> tmpfs = {{"/same", std::nullopt}};
        REQUIRE(!uenv::impl::build_mount_order({}, bind_mounts, tmpfs));
    }

    // an identical destination used twice within the same vector is also
    // rejected
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/src1", "/same"},
                                                         {"/src2", "/same"}};
        const std::vector<tmpfs_mount> tmpfs = {};
        REQUIRE(!uenv::impl::build_mount_order({}, bind_mounts, tmpfs));
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
        const std::vector<tmpfs_mount> tmpfs = {
            {"/user-environment/scratch", std::nullopt},
            {"/data", std::nullopt}};
        auto order = uenv::impl::build_mount_order(mounts, bind_mounts, tmpfs);
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
        auto order = uenv::impl::build_mount_order(mounts, {}, {});
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
        const std::vector<tmpfs_mount> tmpfs = {{"/same", std::nullopt}};
        REQUIRE(!uenv::impl::build_mount_order(mounts, {}, tmpfs));
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
    using uenv::tmpfs_mount;

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
        auto order = uenv::impl::build_mount_order(mounts, bind_mounts, {});
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
        const std::vector<tmpfs_mount> tmpfs = {{"/scratch", std::nullopt}};
        auto order = uenv::impl::build_mount_order({}, bind_mounts, tmpfs);
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
        auto order = uenv::impl::build_mount_order({}, bind_mounts, {});
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
        auto order = uenv::impl::build_mount_order(mounts, bind_mounts, {});
        REQUIRE(order);
        require_order(*order, {
                                  {mount_kind::bind_mount, 0}, // /opt
                                  {mount_kind::sqfs, 0},       // /opt/env
                              });
    }

    // a source inside its own destination
    {
        const std::vector<bindmount_pair> bind_mounts = {{"/d/sub", "/d"}};
        REQUIRE(uenv::impl::build_mount_order({}, bind_mounts, {}));
    }
}

namespace {

// The inputs of build_mount_order, for the tests below.
struct mount_input {
    uenv::mount_list mounts;
    std::vector<uenv::bindmount_pair> bind_mounts;
    std::vector<uenv::tmpfs_mount> tmpfs;

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
        return uenv::impl::build_mount_order(mounts, bind_mounts, tmpfs);
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

// make_mount_plan puts every path in the same canonical form: absolute,
// lexically normal and without a trailing separator, so that the mount order
// and the duplicate check compare like with like.
TEST_CASE("make_mount_plan: canonical paths", "[mount]") {
    namespace fs = std::filesystem;
    auto tmpdir = fs::weakly_canonical(util::make_temp_dir().value());
    fs::create_directories(tmpdir / "sub");
    fs::create_directories(tmpdir / "src");
    const auto dir = tmpdir.string();

    // a trailing separator is dropped
    {
        auto plan = plan_mounts({}, {}, dir + "/");
        REQUIRE(plan);
        REQUIRE(plan->tmpfs.at(0).mount == tmpdir);
    }
    {
        auto plan = plan_mounts({}, dir + "/src/:" + dir + "/sub/");
        REQUIRE(plan);
        REQUIRE(plan->bind_mounts.at(0).src == tmpdir / "src");
        REQUIRE(plan->bind_mounts.at(0).dst == tmpdir / "sub");
    }
    // also after a part of the path that does not exist (which is fine for a
    // mount point inside a --tmpfs)
    {
        auto plan = plan_mounts({}, {}, dir + "," + dir + "/missing/deeper/");
        REQUIRE(plan);
        REQUIRE(plan->tmpfs.at(1).mount == tmpdir / "missing" / "deeper");
    }
    // "." and ".." components are resolved
    {
        auto plan = plan_mounts({}, {}, dir + "/./sub/..");
        REQUIRE(plan);
        REQUIRE(plan->tmpfs.at(0).mount == tmpdir);
    }
    {
        auto plan =
            plan_mounts({}, dir + "/sub/../src/.:" + dir + "/src/../sub/.");
        REQUIRE(plan);
        REQUIRE(plan->bind_mounts.at(0).src == tmpdir / "src");
        REQUIRE(plan->bind_mounts.at(0).dst == tmpdir / "sub");
    }
    // a path relative to the working directory is made absolute, for a
    // --sqfs mount point, a --tmpfs and a --bind-mount alike
    {
        std::ofstream{tmpdir / "img.squashfs"} << "hsqsx";
        fs::create_directories(tmpdir / "mnt");
        fs::create_directories(tmpdir / "dst");
        auto cwd = fs::current_path();
        fs::current_path(tmpdir);
        auto plan = plan_mounts("./img.squashfs:./mnt", "./src:./dst", "./sub");
        fs::current_path(cwd);
        REQUIRE(plan);
        REQUIRE(plan->mounts.at(0).sqfs == tmpdir / "img.squashfs");
        REQUIRE(plan->mounts.at(0).mount == tmpdir / "mnt");
        REQUIRE(plan->tmpfs.at(0).mount == tmpdir / "sub");
        REQUIRE(plan->bind_mounts.at(0).src == tmpdir / "src");
        REQUIRE(plan->bind_mounts.at(0).dst == tmpdir / "dst");
    }

    fs::remove_all(tmpdir);
}

// Destinations are compared as written: the order and the duplicate check
// only work if every destination is spelled the same way, i.e. absolute,
// lexically normal and without a trailing separator.
TEST_CASE("make_mount_plan: order and duplicates", "[mount]") {
    using uenv::mount_kind;

    auto base =
        std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "a" / "b");
    std::filesystem::create_directories(base / "z");
    std::filesystem::create_directories(base / "host");
    auto sqfs = base / "img.squashfs";
    std::ofstream{sqfs} << "hsqsx";
    const auto dir = base.string();
    const auto host = dir + "/host";

    // a trailing separator: "<base>/a/" and "<base>/a" are the same mount
    // point, so using both is a duplicate
    {
        auto plan = plan_mounts({}, host + ":" + dir + "/a", dir + "/a/");
        REQUIRE(!plan);
        REQUIRE_THAT(plan.error(), Catch::Matchers::ContainsSubstring(
                                       "is used by more than one"));
    }

    // "." and ".." components: "<base>/a/./b" and "<base>/a/b" are the same
    REQUIRE(!plan_mounts({}, host + ":" + dir + "/z/../a/b", dir + "/a/./b"));

    // "<base>/z/../a" is the parent of "<base>/a/b", although it sorts after
    // it as written
    {
        auto plan = plan_mounts({}, host + ":" + dir + "/a/b", dir + "/z/../a");
        REQUIRE(plan);
        REQUIRE(plan->order.size() == 2);
        REQUIRE(plan->order[0].kind == mount_kind::tmpfs);
        REQUIRE(plan->order[1].kind == mount_kind::bind_mount);
    }

    // a --sqfs mount point is normalised the same way: "<base>/a/" duplicates
    // a --tmpfs at "<base>/a"
    {
        auto plan = plan_mounts(sqfs.string() + ":" + dir + "/a/");
        REQUIRE(plan);
        REQUIRE(plan->mounts.size() == 1);
        REQUIRE(plan->mounts[0].mount == base / "a");
        REQUIRE(
            !plan_mounts(sqfs.string() + ":" + dir + "/a/", {}, dir + "/a"));
    }

    // two images at the same mount point
    REQUIRE(
        !plan_mounts(fmt::format("{0}:{1}/a,{0}:{1}/a/", sqfs.string(), dir)));

    // a symlinked spelling: "<base>/link/a" is "<base>/real/a" when
    // link -> real, for a --sqfs mount point and a --tmpfs alike
    {
        std::filesystem::create_directories(base / "real" / "a");
        std::filesystem::create_directory_symlink("real", base / "link");
        REQUIRE(plan_mounts(sqfs.string() + ":" + dir + "/link/a"));
        REQUIRE(!plan_mounts(sqfs.string() + ":" + dir + "/link/a", {},
                             dir + "/real/a"));
        // and the other way round
        REQUIRE(!plan_mounts(sqfs.string() + ":" + dir + "/real/a", {},
                             dir + "/link/a"));
    }

    // the images are sorted by mount point, whatever order they were given
    // in, and the order covers every entry
    {
        auto plan = plan_mounts(
            fmt::format("{0}:{1}/a/b,{0}:{1}/a", sqfs.string(), dir), {},
            dir + "/z");
        REQUIRE(plan);
        REQUIRE(plan->mounts.at(0).mount == base / "a");
        REQUIRE(plan->mounts.at(1).mount == base / "a" / "b");
        REQUIRE(plan->order.size() == 3);
        REQUIRE(plan->order[0].kind == mount_kind::sqfs);
        REQUIRE(plan->order[0].index == 0);
        REQUIRE(plan->order[1].kind == mount_kind::sqfs);
        REQUIRE(plan->order[1].index == 1);
        REQUIRE(plan->order[2].kind == mount_kind::tmpfs);
    }

    // a mount point that is missing on the host is an error, unless it is
    // inside a --tmpfs
    REQUIRE(!plan_mounts(sqfs.string() + ":" + dir + "/z/new"));
    REQUIRE(plan_mounts(sqfs.string() + ":" + dir + "/z/new", {}, dir + "/z"));

    // a bind-mount source has to exist
    REQUIRE(!plan_mounts({}, dir + "/absent:" + dir + "/z"));

    // parse errors say which option they are from
    {
        auto plan = plan_mounts({}, {}, "relative:big");
        REQUIRE(!plan);
        REQUIRE_THAT(plan.error(), Catch::Matchers::ContainsSubstring(
                                       "failed to parse tmpfs"));
        plan = plan_mounts({}, dir + "/host", {});
        REQUIRE(!plan);
        REQUIRE_THAT(plan.error(), Catch::Matchers::ContainsSubstring(
                                       "failed to parse bind mounts"));
    }

    // nothing requested is an empty plan
    {
        auto plan = plan_mounts({});
        REQUIRE(plan);
        REQUIRE(plan->order.empty());
    }

    std::filesystem::remove_all(base);
}

TEST_CASE("mount_point_providers", "[mount]") {
    using uenv::bindmount_pair;
    using uenv::mount_kind;
    using uenv::mount_pair;
    using uenv::tmpfs_mount;

    // the provider of each entry, in mount order
    auto providers = [](const uenv::mount_list& mounts,
                        const std::vector<bindmount_pair>& bind_mounts,
                        const std::vector<tmpfs_mount>& tmpfs) {
        auto order = uenv::impl::build_mount_order(mounts, bind_mounts, tmpfs);
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
    using uenv::tmpfs_mount;

    auto base =
        std::filesystem::weakly_canonical(util::make_temp_dir().value());
    std::filesystem::create_directories(base / "exists");
    const auto exists = base / "exists";
    const auto missing = base / "missing";
    // a bind-mount source that exists, with the mount point that the first
    // test below puts inside a bind of it onto `base`
    const auto src = base / "src";
    std::filesystem::create_directories(src / "exists");

    // mount points that exist on the host
    REQUIRE(uenv::impl::validate_mount_points(
        {mount_pair{.sqfs = "/img.squashfs", .mount = exists}}, {{src, base}},
        {}));
    REQUIRE(uenv::impl::validate_mount_points({}, {{src, exists}},
                                              {{base, std::nullopt}}));

    // a duplicate destination is reported before any path is checked
    {
        auto r = uenv::impl::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = missing}},
            {{missing, missing}}, {});
        REQUIRE(!r);
        REQUIRE(r.error() ==
                fmt::format("the destination {} is used by more than one "
                            "--sqfs/--bind-mount/--tmpfs",
                            missing.string()));
    }

    // a mount point missing on the host is an error, whatever kind of mount
    // it is for
    {
        auto r = uenv::impl::validate_mount_points({}, {},
                                                   {{missing, std::nullopt}});
        REQUIRE(!r);
        REQUIRE(r.error() == fmt::format("the mount point {} does not exist",
                                         missing.string()));
        REQUIRE(!uenv::impl::validate_mount_points({}, {{src, missing}}, {}));
        REQUIRE(!uenv::impl::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = missing}}, {}, {}));
        // also one several levels deep, or under a file
        REQUIRE(!uenv::impl::validate_mount_points(
            {}, {}, {{missing / "new/deeper", std::nullopt}}));
        std::ofstream(base / "file").close();
        REQUIRE(!uenv::impl::validate_mount_points(
            {}, {}, {{base / "file/new", std::nullopt}}));
    }

    // a missing mount point inside a --tmpfs or --sqfs is provided by that
    // mount, and is not checked on the host
    {
        const std::vector<tmpfs_mount> tmpfs = {{exists, std::nullopt}};
        REQUIRE(uenv::impl::validate_mount_points({}, {{src, exists / "new"}},
                                                  tmpfs));
        REQUIRE(uenv::impl::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = exists / "new"}}, {},
            tmpfs));
        REQUIRE(uenv::impl::validate_mount_points(
            {}, {}, {{exists, std::nullopt}, {exists / "new", std::nullopt}}));
        REQUIRE(uenv::impl::validate_mount_points(
            {}, {{src, exists / "new/x"}},
            {{exists, std::nullopt}, {exists / "new", std::nullopt}}));
    }
    {
        const uenv::mount_list mounts = {
            mount_pair{.sqfs = "/img.squashfs", .mount = exists}};
        REQUIRE(uenv::impl::validate_mount_points(
            mounts, {}, {{exists / "in-image", std::nullopt}}));
    }

    // a mount point inside a --bind-mount is the path below its source on
    // the host, so it is checked, and never created
    {
        std::filesystem::remove_all(src / "in-source");
        auto r = uenv::impl::validate_mount_points(
            {}, {{src, exists}}, {{exists / "in-source", std::nullopt}});
        REQUIRE(!r);
        REQUIRE(r.error() ==
                fmt::format("the mount point {} is inside "
                            "the --bind-mount {}:{}, where {} does not exist",
                            (exists / "in-source").string(), src.string(),
                            exists.string(), (src / "in-source").string()));
        // the reason is the one check_dir gives, not always "does not exist"
        std::ofstream(src / "in-source").close();
        r = uenv::impl::validate_mount_points(
            {}, {{src, exists}}, {{exists / "in-source", std::nullopt}});
        REQUIRE(!r);
        REQUIRE(r.error() ==
                fmt::format("the mount point {} is inside the --bind-mount "
                            "{}:{}, where {} is not a directory",
                            (exists / "in-source").string(), src.string(),
                            exists.string(), (src / "in-source").string()));
        std::filesystem::remove(src / "in-source");
        std::filesystem::create_directories(src / "in-source");
        REQUIRE(uenv::impl::validate_mount_points(
            {}, {{src, exists}}, {{exists / "in-source", std::nullopt}}));
        // only the innermost provider counts: a --tmpfs inside the bind
        // provides what is below it
        REQUIRE(uenv::impl::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs",
                        .mount = exists / "in-source/new"}},
            {{src, exists}}, {{exists / "in-source", std::nullopt}}));
        // and the provider is the bind, not the host path it covers
        std::filesystem::create_directories(exists / "host-only");
        REQUIRE(!uenv::impl::validate_mount_points(
            {}, {{src, exists}}, {{exists / "host-only", std::nullopt}}));
        std::filesystem::remove_all(src / "in-source");
    }

    // but the provider itself has to exist
    REQUIRE(!uenv::impl::validate_mount_points({}, {{src, missing / "new"}},
                                               {{missing, std::nullopt}}));

    // a bind-mount source on the host must be an existing directory
    {
        auto r = uenv::impl::validate_mount_points({}, {{missing, exists}}, {});
        REQUIRE(!r);
        REQUIRE(r.error() ==
                fmt::format("the bind-mount source {} does not exist",
                            missing.string()));
        std::ofstream(base / "src-file").close();
        REQUIRE(!uenv::impl::validate_mount_points(
            {}, {{base / "src-file", exists}}, {}));
        // inside another destination: still the host's directory, checked
        REQUIRE(!uenv::impl::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = exists}},
            {{exists / "in-image", src}}, {}));
        REQUIRE(!uenv::impl::validate_mount_points(
            {}, {{exists / "in-tmpfs", src}}, {{exists, std::nullopt}}));
        std::filesystem::create_directories(exists / "on-host");
        REQUIRE(uenv::impl::validate_mount_points(
            {mount_pair{.sqfs = "/img.squashfs", .mount = exists}},
            {{exists / "on-host", src}}, {}));
        REQUIRE(uenv::impl::validate_mount_points(
            {}, {{exists / "on-host", src}}, {{exists, std::nullopt}}));
        // inside its own destination: the host path, checked
        REQUIRE(!uenv::impl::validate_mount_points(
            {}, {{exists / "missing", exists}}, {}));
    }

    std::filesystem::remove_all(base);
}

TEST_CASE("validate_mounts error messages", "[mount]") {
    namespace fs = std::filesystem;

    auto root = util::make_temp_dir().value();
    auto sqfs = root / "image.squashfs";
    std::ofstream{sqfs} << "hsqsx";
    auto mount = util::make_temp_dir().value();

    auto validate = [&sqfs](const fs::path& m) {
        return plan_mounts(fmt::format("{}:{}", sqfs, m));
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
    // path is resolved, before the mount point is examined. A path
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
