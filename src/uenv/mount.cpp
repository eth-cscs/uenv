#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/mount.h>
#include <util/expected.h>
#include <util/fs.h>

namespace uenv {

namespace {
// The canonical form for every --sqfs, --tmpfs and --bind-mount path:
// absolute (a relative path is resolved against the working directory),
// lexically normal, with symlinks in the part that exists resolved, and
// without a trailing separator. The mount order and its duplicate check
// (build_mount_order) rely on every path having been through this.
util::expected<std::filesystem::path, std::string>
canonical_path(const std::filesystem::path& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // weakly_canonical alone leaves a relative path relative when its first
    // component does not exist. The two errors are checked separately: an
    // empty path is an error of absolute, and the second call resets ec.
    const auto absolute = fs::absolute(p, ec);
    if (ec) {
        return util::unexpected{ec.message()};
    }
    auto result = fs::weakly_canonical(absolute, ec);
    if (ec) {
        return util::unexpected{ec.message()};
    }
    // weakly_canonical keeps a trailing separator when the end of the path
    // does not exist: "/a/missing/" would not compare equal to "/a/missing"
    // this matters for `build_mount_order`
    if (!result.has_filename() && result != result.root_path()) {
        result = result.parent_path();
    }
    return result;
}

// A mount_description has string descriptions of the squashfs file path and
// mount path taken from parsing a CLI argument or environment variable.
// Convert this to a mount_pair by converting these to std::filesystem::path,
// and validating that the squashfs file exists and can be read.
// The existance of the mount points is not checked, because these need to be
// checked when mounting.
// Note: the squashfs validation here is advisory - it produces a fast, clear
// error before anything is mounted. It is NOT security-relevant, because the
// path can change between this check and the mount. The authoritative check is
// performed on the exact fd that is bound to the loop device, in
// attach_loop_device().
// Both privileged kernel-backend callers (the setuid helper and the Slurm
// plugin) run this function with the calling user's credentials, so every path
// it touches, and every error message it produces, is limited to what that
// user could have discovered for themselves. Do not move it back above a
// privilege elevation: as root it becomes an existence/type/first-4-bytes
// oracle for arbitrary paths.
util::expected<mount_pair, std::string>
make_mount_pair(const mount_description& d) {
    const auto mount = canonical_path(d.mount_path);
    if (!mount) {
        return util::unexpected{fmt::format("invalid mount point {} ({})",
                                            d.mount_path, mount.error())};
    }

    const auto sqfs = canonical_path(d.sqfs_path);
    if (!sqfs) {
        return util::unexpected{
            fmt::format("invalid squashfs {} ({})", d.sqfs_path, sqfs.error())};
    }

    // Open the image and check it on the descriptor, which is then closed:
    // the backends open the image again when they mount it. Give the reason
    // for a failure: "unable to read" on its own sends users looking for a
    // corrupt image when they have no permission to read it.
    if (auto fd = open_squashfs(sqfs.value()); !fd) {
        return util::unexpected{fmt::format("unable to read squashfs {} ({})",
                                            sqfs->string(), fd.error())};
    }

    return mount_pair{.sqfs = sqfs.value(), .mount = mount.value()};
}

util::expected<tmpfs_mount, std::string>
make_tmpfs_mount(const tmpfs_description& d) {
    auto mount = canonical_path(d.mount);
    if (!mount) {
        return util::unexpected{
            fmt::format("invalid mount point {} ({})", d.mount, mount.error())};
    }
    return tmpfs_mount{.mount = std::move(mount.value()), .size = d.size};
}

util::expected<bindmount_pair, std::string>
make_bindmount_pair(const bindmount_description& d) {
    auto src = canonical_path(d.src);
    if (!src) {
        return util::unexpected{fmt::format("invalid bind-mount source {} ({})",
                                            d.src, src.error())};
    }
    auto dst = canonical_path(d.dst);
    if (!dst) {
        return util::unexpected{fmt::format(
            "invalid bind-mount destination {} ({})", d.dst, dst.error())};
    }
    return bindmount_pair{.src = std::move(src.value()),
                          .dst = std::move(dst.value())};
}

// One --sqfs, --bind-mount or --tmpfs destination, and the entry it belongs to.
struct destination {
    const std::filesystem::path* path;
    mount_kind kind;
    // index into the mounts, bind_mounts or tmpfs vector, per `kind`
    std::size_t index;
};

// Every --sqfs, --bind-mount and --tmpfs destination, in lexical order (see
// build_mount_order). An error if two of them are identical, so that nothing
// downstream has to handle a path that encloses itself.
util::expected<std::vector<destination>, std::string>
sorted_destinations(const mount_list& mounts,
                    const std::vector<bindmount_pair>& bind_mounts,
                    const std::vector<tmpfs_mount>& tmpfs) {
    std::vector<destination> result;
    result.reserve(mounts.size() + bind_mounts.size() + tmpfs.size());
    for (std::size_t i = 0; i < mounts.size(); ++i) {
        result.push_back({&mounts[i].mount, mount_kind::sqfs, i});
    }
    for (std::size_t i = 0; i < bind_mounts.size(); ++i) {
        result.push_back({&bind_mounts[i].dst, mount_kind::bind_mount, i});
    }
    for (std::size_t i = 0; i < tmpfs.size(); ++i) {
        result.push_back({&tmpfs[i].mount, mount_kind::tmpfs, i});
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const auto& lhs, const auto& rhs) {
                         return lhs.path->compare(*rhs.path) < 0;
                     });

    for (std::size_t i = 1; i < result.size(); ++i) {
        if (*result[i - 1].path == *result[i].path) {
            return util::unexpected{fmt::format(
                "the destination {} is used by more than one --sqfs/"
                "--bind-mount/--tmpfs",
                result[i].path->string())};
        }
    }
    return result;
}

// The index of the innermost entry of `d` that d[i] lies strictly below, if
// any. `d` must be in lexical order without duplicates (sorted_destinations):
// then every destination that encloses d[i] comes before it, and the deeper
// of two enclosing destinations comes after the shallower one, so the first
// enclosing destination found scanning backwards from i is the innermost.
std::optional<std::size_t>
innermost_enclosing(const std::vector<destination>& d, std::size_t i) {
    for (std::size_t k = i; k-- > 0;) {
        if (util::is_child(*d[i].path, *d[k].path)) {
            return k;
        }
    }
    return std::nullopt;
}

// for each entry of `d`, the innermost other destination that encloses it,
// or nullopt if it is on the host (see mount_point_providers)
std::vector<std::optional<destination>>
enclosing_destinations(const std::vector<destination>& d) {
    std::vector<std::optional<destination>> result(d.size());
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (auto k = innermost_enclosing(d, i)) {
            result[i] = d[*k];
        }
    }
    return result;
}

// The order to mount `destinations` (from sorted_destinations) in. Each entry
// is mounted after the mount that provides its mount point, which the lexical
// order already guarantees: paths compare component by component, so a
// destination sorts before every destination below it. A --bind-mount source
// or a squashfs image adds no constraint: each is taken from the host before
// anything is mounted (see capture_sources in mount_rootless.cpp, and
// open_images in mount_kernel.cpp).
mount_order mount_order_of(const std::vector<destination>& destinations) {
    mount_order order;
    order.reserve(destinations.size());
    for (const auto& d : destinations) {
        order.push_back({d.kind, d.index});
    }
    return order;
}

// The checks of validate_mount_points, on `destinations` from
// sorted_destinations.
util::expected<void, std::string>
check_mount_points(const std::vector<destination>& destinations,
                   const std::vector<bindmount_pair>& bind_mounts) {
    const auto enclosing = enclosing_destinations(destinations);

    // a mount point must exist on the host, unless it is below another
    // requested destination (which provides it, see mount_point_providers)
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        if (enclosing[i]) {
            continue;
        }
        const auto& path = *destinations[i].path;
        if (auto r = util::check_dir(path); !r) {
            return util::unexpected{
                fmt::format("the mount point {} {}", path.string(), r.error())};
        }
    }

    // a bind-mount source is always the host's directory, also when it is
    // inside another destination
    for (const auto& b : bind_mounts) {
        if (auto r = util::check_dir(b.src); !r) {
            return util::unexpected{fmt::format("the bind-mount source {} {}",
                                                b.src.string(), r.error())};
        }
    }

    // a mount point inside a --bind-mount is never created (that would write
    // into its source on the host), and is the same path below the bind's
    // source on the host, so it can be checked before anything is mounted
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        if (!enclosing[i] || enclosing[i]->kind != mount_kind::bind_mount) {
            continue;
        }
        const auto& path = *destinations[i].path;
        const auto& b = bind_mounts[enclosing[i]->index];
        const auto host = b.src / path.lexically_relative(b.dst);
        if (auto r = util::check_dir(host); !r) {
            return util::unexpected{
                fmt::format("the mount point {} is inside the "
                            "--bind-mount {}:{}, where {} {}",
                            path.string(), b.src.string(), b.dst.string(),
                            host.string(), r.error())};
        }
    }
    return {};
}
} // namespace

//
// The two steps of make_mount_plan on canonical entries. They are not part of
// the interface in mount.h: the unit tests declare them.
//
namespace impl {

// Order --sqfs, --bind-mount and --tmpfs entries so that each is mounted
// after the mount that provides its mount point (parents before children),
// regardless of which flag was used for which or what order they were given
// on the command line. A --bind-mount or --tmpfs can therefore be placed
// inside a squashfs image, and a squashfs image inside a --tmpfs.
//
// The order is the lexical order of the destinations, which already puts
// parents first: paths compare component by component, so a path sorts
// before every path below it. Nothing else constrains the order: a
// --bind-mount source or a squashfs image is always the host's, taken before
// anything is mounted, also when it lies inside another destination (see
// capture_sources in mount_rootless.cpp).
//
// Errors: two entries with an identical destination.
util::expected<mount_order, std::string>
build_mount_order(const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_mount>& tmpfs) {
    const auto destinations = sorted_destinations(mounts, bind_mounts, tmpfs);
    if (!destinations) {
        return util::unexpected{destinations.error()};
    }
    return mount_order_of(destinations.value());
}

} // namespace impl

const std::filesystem::path&
mount_destination(const mount_order_entry& e, const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_mount>& tmpfs) {
    switch (e.kind) {
    case mount_kind::sqfs:
        return mounts.at(e.index).mount;
    case mount_kind::bind_mount:
        return bind_mounts.at(e.index).dst;
    case mount_kind::tmpfs:
        break;
    }
    return tmpfs.at(e.index).mount;
}

std::vector<std::optional<mount_kind>> mount_point_providers(
    const mount_list& mounts, const std::vector<bindmount_pair>& bind_mounts,
    const std::vector<tmpfs_mount>& tmpfs, const mount_order& order) {
    // `order` comes from build_mount_order: lexical, without duplicates
    std::vector<destination> destinations;
    destinations.reserve(order.size());
    for (const auto& e : order) {
        destinations.push_back(
            {&mount_destination(e, mounts, bind_mounts, tmpfs), e.kind,
             e.index});
    }
    std::vector<std::optional<mount_kind>> providers(order.size());
    const auto enclosing = enclosing_destinations(destinations);
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (enclosing[i]) {
            providers[i] = enclosing[i]->kind;
        }
    }
    return providers;
}

namespace impl {

// Check the mount point of every --sqfs, --bind-mount and --tmpfs entry that
// is on the host, or inside a --bind-mount. One inside a --sqfs or --tmpfs
// destination is provided by that mount (see mount_point_providers), and can
// not be checked before it is mounted. Paths must be in canonical form
// (canonical_path).
//
// Errors, in this order: two entries with an identical destination (the same
// error as build_mount_order), then the first mount point or source that
// fails one of the checks below.
//
// A mount point on the host must exist.
//
// Every --bind-mount source must be an existing directory on the host, also
// one inside another requested destination: a bind source is always the
// host's directory (see build_mount_order). So a mount point whose innermost
// provider is a --bind-mount is checked too, at the same path below the
// bind's source: it is never created, as that would write into the source on
// the host.
util::expected<void, std::string>
validate_mount_points(const mount_list& mounts,
                      const std::vector<bindmount_pair>& bind_mounts,
                      const std::vector<tmpfs_mount>& tmpfs) {
    // two entries with the same destination are reported before any path is
    // checked: that error does not depend on what is on the host
    const auto destinations = sorted_destinations(mounts, bind_mounts, tmpfs);
    if (!destinations) {
        return util::unexpected{destinations.error()};
    }
    return check_mount_points(destinations.value(), bind_mounts);
}

} // namespace impl

util::expected<mount_plan, std::string>
make_mount_plan(const mount_request& request) {
    mount_plan plan;

    plan.mounts.reserve(request.sqfs.size());
    for (const auto& d : request.sqfs) {
        auto m = make_mount_pair(d);
        if (!m) {
            return util::unexpected{
                fmt::format("invalid squashfs mount {}:{} - {}", d.sqfs_path,
                            d.mount_path, m.error())};
        }
        plan.mounts.push_back(std::move(m.value()));
    }
    // parents before the images mounted inside them: the kernel backend
    // mounts the images in this order
    std::stable_sort(plan.mounts.begin(), plan.mounts.end(),
                     [](const auto& lhs, const auto& rhs) {
                         return lhs.mount.compare(rhs.mount) < 0;
                     });
    for (std::size_t i = 1; i < plan.mounts.size(); ++i) {
        for (std::size_t k = 0; k < i; ++k) {
            if (util::is_child(plan.mounts[i].mount, plan.mounts[k].mount)) {
                spdlog::warn("the mount {} is inside another mount {}",
                             plan.mounts[i].mount, plan.mounts[k].mount);
                break;
            }
        }
    }

    plan.bind_mounts.reserve(request.bind_mounts.size());
    for (const auto& d : request.bind_mounts) {
        auto b = make_bindmount_pair(d);
        if (!b) {
            return util::unexpected{b.error()};
        }
        plan.bind_mounts.push_back(std::move(b.value()));
    }

    plan.tmpfs.reserve(request.tmpfs.size());
    for (const auto& d : request.tmpfs) {
        auto t = make_tmpfs_mount(d);
        if (!t) {
            return util::unexpected{t.error()};
        }
        plan.tmpfs.push_back(std::move(t.value()));
    }

    const auto destinations =
        sorted_destinations(plan.mounts, plan.bind_mounts, plan.tmpfs);
    if (!destinations) {
        return util::unexpected{destinations.error()};
    }
    if (auto r = check_mount_points(destinations.value(), plan.bind_mounts);
        !r) {
        return util::unexpected{r.error()};
    }
    plan.order = mount_order_of(destinations.value());

    return plan;
}

util::expected<void, std::string> check_squashfs_fd(int fd) {
    struct stat st = {};
    if (::fstat(fd, &st) != 0) {
        return util::unexpected{std::string{strerror(errno)}};
    }
    if (!S_ISREG(st.st_mode)) {
        return util::unexpected{std::string{"is not a regular file"}};
    }

    // a squashfs image starts with the magic number "hsqs"
    constexpr std::string_view squashfs_magic{"hsqs"};
    std::array<char, 4> magic{};
    ssize_t n = 0;
    do {
        n = ::pread(fd, magic.data(), magic.size(), 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        return util::unexpected{std::string{strerror(errno)}};
    }
    if (n != static_cast<ssize_t>(magic.size()) ||
        std::string_view{magic.data(), magic.size()} != squashfs_magic) {
        return util::unexpected{std::string{"not a valid squashfs file"}};
    }
    return {};
}

util::expected<util::unique_fd, std::string>
open_squashfs(const std::filesystem::path& path, int flags) {
    // O_NONBLOCK: the open of a FIFO for reading returns at once instead of
    // waiting for a writer. It has no effect on the open of a regular file.
    // O_NOCTTY: a terminal does not become the controlling terminal.
    util::unique_fd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK |
                                                O_NOCTTY | flags)};
    if (!fd) {
        return util::unexpected{std::string{strerror(errno)}};
    }
    if (auto r = check_squashfs_fd(fd.get()); !r) {
        return util::unexpected{r.error()};
    }

    // O_NONBLOCK was for the open: the descriptor is a regular file's, and is
    // handed on as an ordinary blocking one
    const int status_flags = ::fcntl(fd.get(), F_GETFL);
    if (status_flags < 0 ||
        ::fcntl(fd.get(), F_SETFL, status_flags & ~O_NONBLOCK) != 0) {
        return util::unexpected{std::string{strerror(errno)}};
    }
    return fd;
}

util::expected<void, std::string> mount(std::optional<std::string> source,
                                        const std::string& dest,
                                        std::optional<std::string> fstype,
                                        unsigned long mountflags,
                                        const void* nullable_data) {
    spdlog::trace("mount({}, {}, {}, {:b})",
                  source ? source.value().c_str() : "null", dest,
                  fstype ? fstype.value().c_str() : "null", mountflags);
    if (::mount(source ? source->c_str() : nullptr, dest.c_str(),
                fstype ? fstype->c_str() : nullptr, mountflags,
                nullable_data) != 0) {
        return util::unexpected(
            fmt::format("mount failed: {}", strerror(errno)));
    }
    return {};
}

util::expected<void, std::string>
mount_tmpfs(std::filesystem::path dst, std::optional<std::uint64_t> size) {
    std::string options = "mode=0755";
    if (size) {
        options = fmt::format("{},size={}", options, size.value());
    }
    return uenv::mount("tmpfs", dst.string(), "tmpfs", MS_NOSUID | MS_NODEV,
                       options.c_str());
}

} // namespace uenv
