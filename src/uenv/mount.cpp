#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include <sys/mount.h>
#include <unistd.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/mount.h>
#include <uenv/parse.h>
#include <util/expected.h>

namespace uenv {

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
    namespace fs = std::filesystem;

    std::error_code ec{};
    const auto mount = canonical_mount_path(d.mount_path);
    if (!mount) {
        return util::unexpected{fmt::format("invalid mount point {} ({})",
                                            d.mount_path, mount.error())};
    }

    const auto sqfs = fs::weakly_canonical(fs::path(d.sqfs_path), ec);
    if (ec) {
        return util::unexpected{
            fmt::format("invalid squashfs {} ({})", d.sqfs_path, ec.message())};
    }

    // Distinguish "cannot look at it" from "is not a regular file". fs::status
    // reports EACCES through `ec` and then is_regular_file(status) is false, so
    // folding the two together would report a file the caller may not read - or
    // whose directory they may not traverse - as "is not a regular file". That
    // is now the ordinary outcome for an image the caller has no access to,
    // because these checks run with the caller's credentials, so the
    // diagnosis has to be accurate.
    const auto status = fs::status(sqfs, ec);
    if (ec) {
        return util::unexpected{fmt::format("unable to read squashfs {} ({})",
                                            sqfs.string(), ec.message())};
    }
    if (!fs::is_regular_file(status)) {
        return util::unexpected{fmt::format(
            "invalid squashfs {} (is not a regular file)", sqfs.string())};
    }

    // A valid squashfs file contains the magic string "hsqs" in the first 4
    // bytes. Use the error_code overload: the throwing one would terminate the
    // setuid helper, which has no handler, if the file went away or became
    // unreadable after the check above.
    if (fs::file_size(sqfs, ec) < 4 || ec) {
        return util::unexpected{fmt::format(
            "unable to read squashfs {} (not a valid squashfs file)",
            sqfs.string())};
    }
    // Note that a file the caller has no access to gets this far: its metadata
    // is readable whenever the containing directory can be traversed, so it is
    // the open below - not the checks above - that reports the refusal. Say
    // why it failed: "unable to read" on its own sends users looking for a
    // corrupt image rather than a permission problem.
    errno = 0;
    if (auto file = std::ifstream{sqfs, std::ios::binary}) {
        std::array<char, 4> magic{};
        file.read(reinterpret_cast<char*>(magic.data()), magic.size());

        // Compare against little-endian 'hsqs'
        if (!(magic[0] == 'h' && magic[1] == 's' && magic[2] == 'q' &&
              magic[3] == 's')) {
            return util::unexpected{fmt::format(
                "unable to read squashfs {} (not a valid squashfs file)",
                sqfs.string())};
        }
    } else {
        const int open_errno = errno;
        return util::unexpected{fmt::format(
            "unable to read squashfs {}{}", sqfs.string(),
            open_errno ? fmt::format(" ({})", strerror(open_errno)) : "")};
    }

    return mount_pair{.sqfs = sqfs, .mount = *mount};
}

bool is_nested_path(const std::filesystem::path& parent,
                    const std::filesystem::path& child) {
    auto rel = child.lexically_relative(parent);
    return !rel.empty() && *rel.begin() != "..";
}

util::expected<std::filesystem::path, std::string>
canonical_mount_path(const std::filesystem::path& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto result = fs::weakly_canonical(p, ec);
    if (ec) {
        return util::unexpected{ec.message()};
    }
    // weakly_canonical keeps a trailing separator when the end of the path
    // does not exist: "/a/missing/" would not compare equal to "/a/missing"
    if (!result.has_filename() && result != result.root_path()) {
        result = result.parent_path();
    }
    return result;
}

namespace {
// The index of the innermost of `dsts` that `path` lies in (or is equal to,
// if `allow_equal`), skipping `self`: the one with the most components.
std::optional<std::size_t>
innermost_enclosing(const std::vector<std::filesystem::path>& dsts,
                    const std::filesystem::path& path, std::size_t self,
                    bool allow_equal) {
    std::optional<std::size_t> result;
    std::size_t depth = 0;
    for (std::size_t k = 0; k < dsts.size(); ++k) {
        if (k == self || !is_nested_path(dsts[k], path) ||
            (!allow_equal && dsts[k] == path)) {
            continue;
        }
        const auto d = static_cast<std::size_t>(
            std::distance(dsts[k].begin(), dsts[k].end()));
        if (!result || d > depth) {
            result = k;
            depth = d;
        }
    }
    return result;
}
} // namespace

util::expected<mount_order, std::string>
build_mount_order(const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_tuple>& tmpfs) {
    struct entry {
        std::filesystem::path dst;
        mount_kind kind;
        std::size_t index;
    };

    std::vector<entry> entries;
    entries.reserve(mounts.size() + bind_mounts.size() + tmpfs.size());
    for (std::size_t i = 0; i < mounts.size(); ++i) {
        entries.push_back({mounts[i].mount, mount_kind::sqfs, i});
    }
    for (std::size_t i = 0; i < bind_mounts.size(); ++i) {
        entries.push_back({bind_mounts[i].dst, mount_kind::bind_mount, i});
    }
    for (std::size_t i = 0; i < tmpfs.size(); ++i) {
        entries.push_back({tmpfs[i].mount, mount_kind::tmpfs, i});
    }

    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto& lhs, const auto& rhs) {
                         return lhs.dst.compare(rhs.dst) < 0;
                     });

    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (entries[i - 1].dst == entries[i].dst) {
            return util::unexpected{fmt::format(
                "the destination {} is used by more than one --sqfs/"
                "--bind-mount/--tmpfs",
                entries[i].dst.string())};
        }
    }

    // Each entry is mounted after the mount that provides its mount point,
    // and a --bind-mount also after the one that provides its source (or is
    // its source). A source inside the bind's own destination is the host
    // path it covers, and adds no dependency.
    const auto n = entries.size();
    std::vector<std::filesystem::path> dsts;
    dsts.reserve(n);
    for (const auto& e : entries) {
        dsts.push_back(e.dst);
    }
    std::vector<std::vector<std::size_t>> dependents(n);
    std::vector<std::size_t> ndeps(n, 0);
    auto add_dependency = [&](std::optional<std::size_t> dep, std::size_t i) {
        if (dep &&
            std::ranges::find(dependents[*dep], i) == dependents[*dep].end()) {
            dependents[*dep].push_back(i);
            ++ndeps[i];
        }
    };
    for (std::size_t i = 0; i < n; ++i) {
        add_dependency(innermost_enclosing(dsts, dsts[i], i, false), i);
        if (entries[i].kind == mount_kind::bind_mount) {
            const auto& src = bind_mounts[entries[i].index].src;
            add_dependency(innermost_enclosing(dsts, src, i, true), i);
        }
    }

    // a topological sort that takes the lexically smallest ready entry, so
    // that the order only differs from the lexical one where it has to
    std::set<std::size_t> ready;
    for (std::size_t i = 0; i < n; ++i) {
        if (ndeps[i] == 0) {
            ready.insert(i);
        }
    }
    mount_order order;
    order.reserve(n);
    while (!ready.empty()) {
        const auto i = *ready.begin();
        ready.erase(ready.begin());
        order.push_back({entries[i].kind, entries[i].index});
        for (auto j : dependents[i]) {
            if (--ndeps[j] == 0) {
                ready.insert(j);
            }
        }
    }
    if (order.size() != n) {
        std::vector<std::string> cycle;
        for (std::size_t i = 0; i < n; ++i) {
            if (ndeps[i] != 0) {
                cycle.push_back(dsts[i].string());
            }
        }
        return util::unexpected{fmt::format(
            "the mounts at {} can not be ordered: a --bind-mount source and a "
            "destination depend on each other",
            fmt::join(cycle, ", "))};
    }
    return order;
}

namespace {
// the destination of every --sqfs, --bind-mount and --tmpfs entry
std::vector<std::filesystem::path>
all_destinations(const mount_list& mounts,
                 const std::vector<bindmount_pair>& bind_mounts,
                 const std::vector<tmpfs_tuple>& tmpfs) {
    std::vector<std::filesystem::path> result;
    result.reserve(mounts.size() + bind_mounts.size() + tmpfs.size());
    for (const auto& m : mounts) {
        result.push_back(m.mount);
    }
    for (const auto& b : bind_mounts) {
        result.push_back(b.dst);
    }
    for (const auto& t : tmpfs) {
        result.push_back(t.mount);
    }
    return result;
}
} // namespace

const std::filesystem::path&
mount_destination(const mount_order_entry& e, const mount_list& mounts,
                  const std::vector<bindmount_pair>& bind_mounts,
                  const std::vector<tmpfs_tuple>& tmpfs) {
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
    const std::vector<tmpfs_tuple>& tmpfs, const mount_order& order) {
    auto dst = [&](const mount_order_entry& e) -> const std::filesystem::path& {
        return mount_destination(e, mounts, bind_mounts, tmpfs);
    };

    std::vector<std::filesystem::path> dsts;
    dsts.reserve(order.size());
    for (const auto& e : order) {
        dsts.push_back(dst(e));
    }
    std::vector<std::optional<mount_kind>> providers(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (auto j = innermost_enclosing(dsts, dsts[i], i, false)) {
            providers[i] = order[*j].kind;
        }
    }
    return providers;
}

namespace {
// the mount points on the host that do not exist: those not below another
// requested destination (which provides them, see mount_point_providers)
std::vector<std::filesystem::path>
missing_host_mount_points(const mount_list& mounts,
                          const std::vector<bindmount_pair>& bind_mounts,
                          const std::vector<tmpfs_tuple>& tmpfs) {
    const auto destinations = all_destinations(mounts, bind_mounts, tmpfs);
    std::vector<std::filesystem::path> result;
    for (const auto& path : destinations) {
        const bool provided =
            std::ranges::any_of(destinations, [&path](const auto& other) {
                return other != path && is_nested_path(other, path);
            });
        if (!provided && !std::filesystem::is_directory(path)) {
            result.push_back(path);
        }
    }
    return result;
}
} // namespace

util::expected<void, std::string> validate_mount_points(
    const mount_list& mounts, const std::vector<bindmount_pair>& bind_mounts,
    const std::vector<tmpfs_tuple>& tmpfs, bool mutable_root) {
    for (const auto& path :
         missing_host_mount_points(mounts, bind_mounts, tmpfs)) {
        if (!mutable_root) {
            return util::unexpected{fmt::format(
                "the mount point {} does not exist", path.string())};
        }
        if (auto ancestor = deepest_existing_ancestor(path); !ancestor) {
            return util::unexpected{ancestor.error()};
        }
    }
    return {};
}

std::vector<std::pair<std::filesystem::path, std::filesystem::path>>
writable_shadowed_dirs(const mount_list& mounts,
                       const std::vector<bindmount_pair>& bind_mounts,
                       const std::vector<tmpfs_tuple>& tmpfs) {
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> result;
    for (const auto& path :
         missing_host_mount_points(mounts, bind_mounts, tmpfs)) {
        auto ancestor = deepest_existing_ancestor(path);
        if (ancestor && *ancestor != ancestor->root_path() &&
            ::access(ancestor->c_str(), W_OK) == 0) {
            result.emplace_back(path, *ancestor);
        }
    }
    return result;
}

util::expected<std::filesystem::path, std::string>
deepest_existing_ancestor(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    fs::path result = path.root_path();
    for (const auto& component : path.relative_path()) {
        const auto next = result / component;
        std::error_code ec;
        const auto status = fs::symlink_status(next, ec);
        if (status.type() == fs::file_type::not_found) {
            break;
        }
        if (ec) {
            return util::unexpected{fmt::format("unable to stat {}: {}",
                                                next.string(), ec.message())};
        }
        if (!fs::is_directory(status)) {
            return util::unexpected{
                fmt::format("the mount point {} can not be created: {} is not "
                            "a directory",
                            path.string(), next.string())};
        }
        result = next;
    }
    return result;
}

util::expected<std::set<std::filesystem::path>, std::string>
mutable_root_shadow_dirs(const std::set<std::filesystem::path>& dsts) {
    std::set<std::filesystem::path> result = {"/"};
    for (const auto& dst : dsts) {
        if (std::filesystem::is_directory(dst)) {
            continue;
        }
        auto ancestor = deepest_existing_ancestor(dst);
        if (!ancestor) {
            return util::unexpected{ancestor.error()};
        }
        for (auto p = *ancestor; p != p.root_path(); p = p.parent_path()) {
            result.insert(p);
        }
    }
    return result;
}

util::expected<std::vector<uenv::mount_pair>, std::string>
validate_mount_list(const mount_list& input, bool mount_points_must_exist) {
    namespace fs = std::filesystem;

    if (input.empty()) {
        return input;
    }

    // Iterate over the mount points and verify whether they exist.
    // There is a wrinkle: a mount point may be "inside" another mount
    // point, and thus rely on the other mount first being mounted before it
    // can be mounted. We first sort the mount entries so that they can be
    // mounted like this, and only check for existance of "root" mounts that
    // are not inside another mount.
    //
    // Note that we do not check for the existance of mount points that have
    // to be provided by another mount. This would be very tricky, without
    // starting to parse squashfs files and their meta data - WHICH WE DO NOT
    // WANT TO DO BECAUSE THIS CODE RUNS IN THE PRIVILAGED HELPER.

    std::vector<uenv::mount_pair> mounts{input};
    std::sort(std::begin(mounts), std::end(mounts),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.mount.compare(rhs.mount) < 0;
              });

    auto mview = std::ranges::transform_view(
        mounts, [](const auto& in) -> const fs::path& { return in.mount; });

    // check whether there are two identical mount points.
    // take advantage of the list being sorted.
    if (auto it = std::adjacent_find(mview.begin(), mview.end());
        it != mview.end()) {
        return util::unexpected{fmt::format(
            "the mount point {} is used to mount more than one squashfs", *it)};
    }

    const auto b = std::begin(mview);
    const auto e = std::cend(mview);

    // check whether the first mount point exists
    if (!fs::is_directory(*b) && mount_points_must_exist) {
        return util::unexpected{
            fmt::format("the mount path {} does not exist", (*b).string())};
    }
    // iterate over the remaining mounts
    for (auto c = b + 1; c != e; ++c) {
        auto parent = std::find_if(
            b, c, [&c](const auto& it) { return is_nested_path(it, *c); });
        if (parent == c) { // there is no parent
            if (!fs::is_directory(*c) && mount_points_must_exist) {
                return util::unexpected{
                    fmt::format("the mount path {} does not exist", *c)};
            }
        } else {
            spdlog::warn("the mount {} is inside another mount {}", *c,
                         *(c - 1));
        }
    }

    return mounts;
}

util::expected<mount_list, std::string>
validate_mount_descriptions(const std::vector<mount_description>& input,
                            bool mount_points_must_exist) {
    mount_list mounts;
    for (auto desc : input) {
        if (auto mount = uenv::make_mount_pair(desc); !mount) {
            return util::unexpected{
                fmt::format("invalid squashfs mount {}:{} - {}", desc.sqfs_path,
                            desc.mount_path, mount.error())};
        } else {
            mounts.push_back(mount.value());
        }
    }

    return validate_mount_list(mounts, mount_points_must_exist);
}

util::expected<mount_list, std::string>
parse_and_validate_mounts(const std::string& description,
                          bool mount_points_must_exist) {
    auto mount_descriptions = uenv::parse_mount_list(description);
    if (!mount_descriptions) {
        return util::unexpected{mount_descriptions.error().message()};
    }

    return validate_mount_descriptions(mount_descriptions.value(),
                                       mount_points_must_exist);
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

util::expected<void, std::string> bind_mount(std::filesystem::path src,
                                             std::filesystem::path dst) {
    spdlog::trace("bind_mount({}, {})", src.string(), dst.string());
    return uenv::mount(src.string(), dst.string(), std::nullopt,
                       MS_NOSUID | MS_BIND | MS_REC | MS_SILENT, nullptr);
}

} // namespace uenv
