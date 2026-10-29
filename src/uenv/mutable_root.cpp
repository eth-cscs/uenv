#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/mount.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <libmount/libmount.h>
#include <spdlog/spdlog.h>

#include <uenv/mount.h>
#include <uenv/mount_rootless.h>
#include <util/expected.h>

namespace uenv {
namespace rootless {

namespace {

// Decode a comma separated VFS options string - the 4th field of a
// /proc/self/mountinfo line, e.g. "rw,nosuid,nodev,relatime" - into MS_*
// flags. Mirrors bwrap's decode_mountoptions():
// https://github.com/containers/bubblewrap/blob/main/bind-mount.c#L82
unsigned long decode_mountoptions(std::string_view options) {
    static const std::pair<std::string_view, unsigned long> flags_data[] = {
        {"ro", MS_RDONLY},         {"nosuid", MS_NOSUID},
        {"nodev", MS_NODEV},       {"noexec", MS_NOEXEC},
        {"noatime", MS_NOATIME},   {"nodiratime", MS_NODIRATIME},
        {"relatime", MS_RELATIME},
    };
    unsigned long flags = 0;
    std::size_t pos = 0;
    while (pos <= options.size()) {
        auto end = options.find(',', pos);
        if (end == std::string_view::npos) {
            end = options.size();
        }
        auto token = options.substr(pos, end - pos);
        for (auto [name, flag] : flags_data) {
            if (token == name) {
                flags |= flag;
                break;
            }
        }
        pos = end + 1;
    }
    return flags;
}

struct mount_node {
    int id{-1};
    int parent_id{-1};
    std::filesystem::path mountpoint;
    unsigned long flags{0};
    bool covered{false};
    std::vector<std::size_t> children;
};

void collect_mounts(const std::vector<mount_node>& nodes, std::size_t idx,
                    std::vector<std::size_t>& out) {
    if (!nodes[idx].covered) {
        out.push_back(idx);
    }
    for (auto child : nodes[idx].children) {
        collect_mounts(nodes, child, out);
    }
}

// A single flattened entry of /proc/self/mountinfo, independent of any
// particular root - the raw material `mounts_under()` builds a per-root
// subtree from. Kept separate from `mount_node` (which additionally carries
// the `covered`/`children` bookkeeping) so that one parse can serve multiple
// `mounts_under()` queries.
struct mount_entry {
    int id{-1};
    int parent_id{-1};
    std::filesystem::path mountpoint;
    std::string fstype;
    unsigned long flags{0};
};

// Parse /oldroot/proc/self/mountinfo
util::expected<std::vector<mount_entry>, std::string> parse_mountinfo() {
    std::unique_ptr<libmnt_table, decltype(&mnt_free_table)> tb(mnt_new_table(),
                                                                mnt_free_table);
    if (!tb) {
        return util::unexpected{"mnt_new_table failed"};
    }
    if (int rc = mnt_table_parse_file(tb.get(), "/oldroot/proc/self/mountinfo");
        rc != 0) {
        return util::unexpected{fmt::format(
            "unable to parse /proc/self/mountinfo: rc={} errno={} ({})", rc,
            errno, strerror(errno))};
    }

    std::unique_ptr<libmnt_iter, decltype(&mnt_free_iter)> itr(
        mnt_new_iter(MNT_ITER_FORWARD), mnt_free_iter);
    if (!itr) {
        return util::unexpected{"mnt_new_iter failed"};
    }

    std::vector<mount_entry> entries;
    libmnt_fs* fs = nullptr;
    while (mnt_table_next_fs(tb.get(), itr.get(), &fs) == 0) {
        mount_entry e;
        e.id = mnt_fs_get_id(fs);
        e.parent_id = mnt_fs_get_parent_id(fs);
        e.mountpoint = mnt_fs_get_target(fs);
        const char* fstype = mnt_fs_get_fstype(fs);
        e.fstype = fstype ? fstype : "";
        const char* vfs_options = mnt_fs_get_vfs_options(fs);
        e.flags = decode_mountoptions(vfs_options ? vfs_options : "");
        entries.push_back(std::move(e));
    }
    return entries;
}

// Build the list of (mountpoint, current flags) for `root` and every mount
// stacked at or below it, from an already-parsed /proc/self/mountinfo
// (`parse_mountinfo()`). Mounts are attached to their real kernel parent
// mount, and a mount stacked directly on top of another mount at the exact
// same path "covers" (hides) the one underneath, mirroring what the kernel
// itself exposes. This is the C++/libmount equivalent of bwrap's
// parse_mountinfo()/collect_mounts():
// https://github.com/containers/bubblewrap/blob/main/bind-mount.c#L207
util::expected<std::vector<std::pair<std::filesystem::path, unsigned long>>,
               std::string>
mounts_under(const std::vector<mount_entry>& entries,
             const std::filesystem::path& root) {
    std::vector<mount_node> nodes;
    std::unordered_map<int, std::size_t> id_to_index;
    nodes.reserve(entries.size());
    for (const auto& e : entries) {
        mount_node n;
        n.id = e.id;
        n.parent_id = e.parent_id;
        n.mountpoint = e.mountpoint;
        n.flags = e.flags;
        id_to_index[n.id] = nodes.size();
        nodes.push_back(std::move(n));
    }

    // As in bwrap: if the same path is mounted more than once, the last one
    // in mountinfo order is the one the kernel currently exposes there.
    std::size_t root_idx = nodes.size();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].mountpoint == root) {
            root_idx = i;
        }
    }
    if (root_idx == nodes.size()) {
        return util::unexpected{
            fmt::format("unable to find {} in the mount table", root.string())};
    }

    // find children
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (i == root_idx || !uenv::is_nested_path(root, nodes[i].mountpoint)) {
            continue;
        }
        auto pit = id_to_index.find(nodes[i].parent_id);
        if (pit == id_to_index.end()) {
            continue;
        }
        mount_node& parent = nodes[pit->second];

        if (parent.mountpoint == nodes[i].mountpoint) {
            parent.covered = true;
        }

        // this mount is nested inside (or equal to) a sibling: the sibling
        // already covers it.
        const bool covered =
            std::any_of(parent.children.begin(), parent.children.end(),
                        [&](std::size_t sidx) {
                            return uenv::is_nested_path(nodes[sidx].mountpoint,
                                                        nodes[i].mountpoint);
                        });
        if (covered) {
            continue;
        }
        // any sibling nested inside this mount is superseded by it.
        std::erase_if(parent.children, [&](std::size_t sidx) {
            return uenv::is_nested_path(nodes[i].mountpoint,
                                        nodes[sidx].mountpoint);
        });
        parent.children.push_back(i);
    }

    std::vector<std::size_t> flat;
    collect_mounts(nodes, root_idx, flat);

    std::vector<std::pair<std::filesystem::path, unsigned long>> result;
    result.reserve(flat.size());
    for (auto idx : flat) {
        result.emplace_back(nodes[idx].mountpoint, nodes[idx].flags);
    }
    return result;
}

// Re-apply MS_NOSUID to `dst` and every mount stacked below it, preserving each
// mount's own other flags (ro/nodev/noexec/atime...). A remount is skipped when
// it would be a no-op. Remount failure is logged and skipped rather than
// aborting make_mutable_root. This is equivalent to what bwrap bind-mount.c is
// doing
util::expected<void, std::string>
apply_nosuid_recursive(const std::vector<mount_entry>& entries,
                       const std::filesystem::path& dst) {
    auto mounts = mounts_under(entries, dst);
    if (!mounts) {
        return util::unexpected{mounts.error()};
    }
    for (auto& [path, current_flags] : *mounts) {
        auto new_flags = current_flags | MS_NOSUID;
        if (new_flags == current_flags) {
            continue;
        }
        if (auto r = uenv::mount("none", path.string(), std::nullopt,
                                 MS_SILENT | MS_BIND | MS_REMOUNT | new_flags,
                                 nullptr);
            !r) {
            spdlog::warn("make_mutable_root: unable to remount {} nosuid: {}",
                         path.string(), r.error());
        }
    }
    return {};
}

// Refuse to shadow a directory on an autofs mount: its entries are mounted
// on demand, so a copy would only hold the ones that happen to be mounted
// now. `entries` is the mount table read after the first pivot_root, where
// the host's mounts are under /oldroot.
util::expected<void, std::string>
check_no_autofs(const std::vector<mount_entry>& entries,
                const std::set<std::filesystem::path>& shadow) {
    for (const auto& dir : shadow) {
        if (dir == dir.root_path()) {
            continue;
        }
        const auto host =
            std::filesystem::path("/oldroot") / dir.relative_path();
        // the mount that dir is on: the innermost one, and of several at the
        // same path, the last one (the one the kernel exposes)
        const mount_entry* mount = nullptr;
        std::ptrdiff_t depth = 0;
        for (const auto& e : entries) {
            if (!uenv::is_nested_path(e.mountpoint, host)) {
                continue;
            }
            const auto d =
                std::distance(e.mountpoint.begin(), e.mountpoint.end());
            if (!mount || d >= depth) {
                mount = &e;
                depth = d;
            }
        }
        if (mount && mount->fstype == "autofs") {
            return util::unexpected{fmt::format(
                "the mutable root can not create a mount point in {}: it is "
                "on an autofs mount",
                dir.string())};
        }
    }
    return {};
}

// Recreate the host directory `dir` (read through /oldroot) at /newroot/dir:
// shadowed directories below it are recreated in turn, other directories are
// bind mounted (appended to `bound`), symlinks are copied, and any other file
// is bind mounted onto an empty file.
util::expected<void, std::string>
shadow_dir(const std::filesystem::path& dir,
           const std::set<std::filesystem::path>& shadow,
           std::vector<std::filesystem::path>& bound) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto src_dir = fs::path("/oldroot") / dir.relative_path();
    const auto dst_dir = fs::path("/newroot") / dir.relative_path();

    if (dir != dir.root_path()) {
        const auto perms = fs::status(src_dir, ec).permissions();
        if (!ec) {
            fs::create_directory(dst_dir, ec);
        }
        if (!ec) {
            fs::permissions(dst_dir, perms, ec);
        }
        if (ec) {
            return util::unexpected{fmt::format("unable to shadow {}: {}",
                                                dir.string(), ec.message())};
        }
    }

    std::size_t count = 0;
    fs::directory_iterator it(src_dir, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (++count > max_shadow_entries) {
            return util::unexpected{fmt::format(
                "the mutable root can not create a mount point in {}: it has "
                "more than {} entries",
                dir.string(), max_shadow_entries)};
        }
        const auto name = it->path().filename();
        const auto src = src_dir / name;
        const auto dst = dst_dir / name;
        // symlink_status: a symlink is copied, never followed, so a dangling
        // link or a link to a file needs no special handling
        const auto type = it->symlink_status(ec).type();
        if (ec) {
            break;
        }
        if (type == fs::file_type::directory && shadow.contains(dir / name)) {
            if (auto r = shadow_dir(dir / name, shadow, bound); !r) {
                return r;
            }
        } else if (type == fs::file_type::symlink) {
            const auto target = fs::read_symlink(src, ec);
            if (!ec) {
                fs::create_symlink(target, dst, ec);
            }
            if (ec) {
                return util::unexpected{
                    fmt::format("unable to recreate symlink {}: {}",
                                (dir / name).string(), ec.message())};
            }
        } else if (type == fs::file_type::directory) {
            fs::create_directory(dst, ec);
            if (ec) {
                return util::unexpected{
                    fmt::format("fs::create_directory({}) failed with {}",
                                dst.string(), ec.message())};
            }
            if (auto r = uenv::mount(src.string(), dst.string(), std::nullopt,
                                     MS_BIND | MS_REC | MS_SILENT, nullptr);
                !r) {
                return r;
            }
            bound.push_back(dst);
        } else {
            // a regular file, fifo, socket or device node, e.g. the empty
            // /.dockerenv marker file some container runtimes create
            std::ofstream(dst).close();
            if (auto r = uenv::mount(src.string(), dst.string(), std::nullopt,
                                     MS_BIND | MS_SILENT, nullptr);
                !r) {
                return r;
            }
        }
    }
    if (ec) {
        return util::unexpected{
            fmt::format("unable to list {}: {}", dir.string(), ec.message())};
    }
    return {};
}
} // namespace

// Rebuild "/" on a tmpfs, shadowing the directories in `shadow` and bind
// mounting everything else, inspired by bubblewrap:
// https://github.com/containers/bubblewrap/blob/main/bind-mount.c#L378
util::expected<void, std::string>
make_mutable_root(const std::set<std::filesystem::path>& shadow) {
    namespace fs = std::filesystem;
    std::error_code ec;

    auto original_path = fs::current_path(ec);
    if (ec) {
        return util::unexpected(
            fmt::format("fs::current_path failed with {}", ec.message()));
    }
    spdlog::info("make mutable root, shadowing {}", fmt::join(shadow, ", "));

    // Recursively make "/" a slave mount so that none of the bind/remount
    // activity below propagates back into the host's mount namespace.
    if (auto r = uenv::mount(std::nullopt, "/", std::nullopt,
                             MS_REC | MS_SILENT | MS_SLAVE, nullptr);
        !r) {
        return r;
    }

    // Stage the new root inside a fresh tmpfs mounted over the *original*
    // /tmp. "newroot" is bind-mounted onto itself so that it is a distinct
    // mount point from its parent tmpfs. This mirrors bwrap's setup_newroot().
    if (auto r = uenv::mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV,
                             nullptr);
        !r) {
        return r;
    }

    fs::current_path("/tmp", ec);
    if (ec) {
        return util::unexpected(
            fmt::format("fs::current_path(/tmp) failed with {}", ec.message()));
    }
    fs::create_directory("newroot", ec);
    if (ec) {
        return util::unexpected(fmt::format(
            "fs::create_directory(newroot) failed with {}", ec.message()));
    }

    if (auto r =
            uenv::mount("newroot", "newroot", std::nullopt,
                        MS_MGC_VAL | MS_BIND | MS_REC | MS_SILENT, nullptr);
        !r) {
        return r;
    }
    fs::create_directory("oldroot", ec);
    if (ec) {
        return util::unexpected(fmt::format(
            "fs::create_directory(oldroot) failed with {}", ec.message()));
    }

    // make /tmp the new root mount, while /oldroot contains the old root
    if (syscall(SYS_pivot_root, "/tmp", "oldroot") != 0) {
        return util::unexpected(fmt::format(
            "pivot_root(\"/tmp\", \"oldroot\") failed: {}", strerror(errno)));
    }
    fs::current_path("/", ec);
    if (ec) {
        return util::unexpected(
            fmt::format("fs::current_path(/) failed with {}", ec.message()));
    }

    auto host_mounts = parse_mountinfo();
    if (!host_mounts) {
        return util::unexpected{host_mounts.error()};
    }
    if (auto r = check_no_autofs(*host_mounts, shadow); !r) {
        return r;
    }

    // "/" and the shadowed directories below it; a relative symlink target
    // resolves against the new "/", which has the same entries
    std::vector<fs::path> bound;
    if (auto r = shadow_dir("/", shadow, bound); !r) {
        return r;
    }

    auto mountinfo = parse_mountinfo();
    if (!mountinfo) {
        return util::unexpected{mountinfo.error()};
    }
    for (const auto& dst : bound) {
        if (auto r = apply_nosuid_recursive(*mountinfo, dst); !r) {
            return r;
        }
    }

    // Teardown, mirroring bwrap's two-step detach of the old root:
    // 1. make "oldroot" (still reachable at "/oldroot") private and
    //    recursively lazy-unmount it while still chdir'd at "/", so the old
    //    root's mounts are dropped without affecting the host namespace.
    // 2. chdir into "/newroot" and pivot_root(".", ".") onto itself, then
    //    lazy-unmount "." again to drop the now-empty self-bind-mount of
    //    "newroot" created earlier.
    if (auto r = uenv::mount("oldroot", "oldroot", std::nullopt,
                             MS_REC | MS_SILENT | MS_PRIVATE, nullptr);
        !r) {
        return r;
    }

    if (umount2("oldroot", MNT_DETACH) != 0) {
        return util::unexpected(fmt::format(
            "umount2(\"oldroot\", MNT_DETACH) failed: {}", strerror(errno)));
    }
    fs::current_path("/newroot", ec);
    if (ec) {
        return util::unexpected(
            fmt::format("current_path(/newroot) failed with {}", ec.message()));
    }

    if (syscall(SYS_pivot_root, ".", ".") != 0) {
        return util::unexpected(fmt::format(
            "pivot_root(\".\", \".\") failed: {}", strerror(errno)));
    }
    if (umount2(".", MNT_DETACH) != 0) {
        return util::unexpected(fmt::format(
            "umount2(\".\", MNT_DETACH) failed: {}", strerror(errno)));
    }
    fs::current_path(original_path, ec);
    if (ec) {
        return util::unexpected(fmt::format(
            "current_path(original_path) failed with {}", ec.message()));
    }

    return {};
}

} // namespace rootless
} // namespace uenv
