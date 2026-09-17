#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/mount.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <fmt/format.h>
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

bool has_path_prefix(const std::filesystem::path& path,
                     const std::filesystem::path& prefix) {
    auto pit = path.begin(), pend = path.end();
    auto qit = prefix.begin(), qend = prefix.end();
    for (; qit != qend; ++pit, ++qit) {
        if (pit == pend || *pit != *qit) {
            return false;
        }
    }
    return true;
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
        if (i == root_idx || !has_path_prefix(nodes[i].mountpoint, root)) {
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
        const bool covered = std::any_of(
            parent.children.begin(), parent.children.end(),
            [&](std::size_t sidx) {
                return has_path_prefix(nodes[i].mountpoint,
                                       nodes[sidx].mountpoint);
            });
        if (covered) {
            continue;
        }
        // any sibling nested inside this mount is superseded by it.
        std::erase_if(parent.children, [&](std::size_t sidx) {
            return has_path_prefix(nodes[sidx].mountpoint, nodes[i].mountpoint);
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
// aborting make_mutable_root. This is equivalent to what bwrap bind-mount.c is doing
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
} // namespace

// Rebuild "/" from bind mounts of everything currently under it, inspired by
// bubblewrap:
// https://github.com/containers/bubblewrap/blob/main/bind-mount.c#L378
util::expected<void, std::string> make_mutable_root() {
    namespace fs = std::filesystem;
    // exlcude the uenv directories, we might want to create directories below
    // them (created writable)
    static const fs::path excluded_topdirs[] = {"/user-environment",
                                                "/user-tools"};
    auto original_path = fs::current_path();
    spdlog::info("make mutable root");
    std::vector<fs::path> topdirs;
    std::vector<fs::path> topfiles;
    std::vector<std::pair<fs::path, fs::path>> files_symlinks;
    for (const auto& entry : fs::directory_iterator("/")) {
        // Check is_symlink() first: it is lstat-based and never throws, even
        // for a dangling target. is_directory() follows the link via
        // status(), which throws ENOENT for exactly that case - so it must
        // only be called once a symlink has already been ruled out.
        if (entry.is_symlink()) {
            files_symlinks.push_back(
                std::make_pair(entry.path(), fs::read_symlink(entry)));
        } else if (entry.is_directory()) {
            if (std::ranges::find(excluded_topdirs, entry.path()) !=
                std::ranges::end(excluded_topdirs)) {
                continue;
            }
            topdirs.push_back(entry.path());
        } else {
            // a regular file, fifo, socket, or device node living directly
            // under "/" - e.g. the empty /.dockerenv marker file some
            // container runtimes create. Not a directory and not a
            // symlink, so it needs its own bind-onto-a-file handling below
            // rather than falling through to read_symlink().
            topfiles.push_back(entry.path());
        }
    }

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

    fs::current_path("/tmp");
    fs::create_directory("newroot");

    if (auto r =
            uenv::mount("newroot", "newroot", std::nullopt,
                        MS_MGC_VAL | MS_BIND | MS_REC | MS_SILENT, nullptr);
        !r) {
        return r;
    }
    fs::create_directory("oldroot");

    // make /tmp the new root mount, while /oldroot contains the old root
    if (syscall(SYS_pivot_root, "/tmp", "oldroot") != 0) {
        return util::unexpected(fmt::format(
            "pivot_root(\"/tmp\", \"oldroot\") failed: {}", strerror(errno)));
    }
    fs::current_path("/");

    for (auto entry : files_symlinks) {
        auto src = fs::path("/oldroot") / entry.second.relative_path();
        auto dst = fs::path("/newroot") / entry.first.relative_path();

        spdlog::debug("src {}, dst {}", src.c_str(), dst.c_str());
        fs::create_directories(dst);
        if (auto r = uenv::mount(src.string(), dst.string(), std::nullopt,
                                 MS_BIND | MS_REC | MS_SILENT, nullptr);
            !r) {
            return r;
        }
    }

    // top-level directories
    std::vector<fs::path> topdir_dsts;
    topdir_dsts.reserve(topdirs.size());
    for (auto entry : topdirs) {
        auto src = fs::path("/oldroot") / entry.relative_path();
        auto dst = fs::path("/newroot") / entry.relative_path();
        fs::create_directory(dst);
        if (auto r = uenv::mount(src.string(), dst.string(), std::nullopt,
                                 MS_BIND | MS_REC | MS_SILENT, nullptr);
            !r) {
            return r;
        }
        topdir_dsts.push_back(std::move(dst));
    }

    // regular files, fifos, sockets, device nodes directly under "/"
    for (const auto& entry : topfiles) {
        auto src = fs::path("/oldroot") / entry.relative_path();
        auto dst = fs::path("/newroot") / entry.relative_path();
        std::ofstream(dst).close();
        if (auto r = uenv::mount(src.string(), dst.string(), std::nullopt,
                                 MS_BIND | MS_SILENT, nullptr);
            !r) {
            return r;
        }
    }

    auto mountinfo = parse_mountinfo();
    if (!mountinfo) {
        return util::unexpected{mountinfo.error()};
    }
    for (const auto& dst : topdir_dsts) {
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
    fs::current_path("/newroot");

    if (syscall(SYS_pivot_root, ".", ".") != 0) {
        return util::unexpected(fmt::format(
            "pivot_root(\".\", \".\") failed: {}", strerror(errno)));
    }
    if (umount2(".", MNT_DETACH) != 0) {
        return util::unexpected(fmt::format(
            "umount2(\".\", MNT_DETACH) failed: {}", strerror(errno)));
    }
    fs::current_path(original_path);

    return {};
}

} // namespace rootless
} // namespace uenv
