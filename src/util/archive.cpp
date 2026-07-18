#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#include <vector>

#include <archive.h>
#include <archive_entry.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <zlib.h>

#include <util/archive.h>
#include <util/defer.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/sha.h>

namespace fs = std::filesystem;

namespace util {

namespace {

// Build the uncompressed, deterministic tar of `dir` at `tar_path`.
//
// The blocking is set so the archive ends exactly at the two 512-byte zero
// blocks that mark end-of-archive, with NO trailing padding.
// This matches what oras itself writes (Go's archive/tar emits only the two
// zero blocks).
util::expected<void, std::string> write_tar(const fs::path& dir,
                                            const fs::path& tar_path) {
    const auto base = dir.parent_path();

    // collect the top directory plus everything beneath it, then sort by the
    // in-archive name so the layout (and therefore the digest) is stable.
    std::vector<fs::path> paths;
    paths.push_back(dir);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::none,
                                             ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        paths.push_back(it->path());
    }
    if (ec) {
        return util::unexpected{
            fmt::format("unable to walk {}: {}", dir, ec.message())};
    }
    std::sort(paths.begin(), paths.end(),
              [&base](const fs::path& a, const fs::path& b) {
                  return a.lexically_relative(base).string() <
                         b.lexically_relative(base).string();
              });

    archive* a = archive_write_new();
    if (a == nullptr) {
        return util::unexpected{"unable to allocate a tar writer"};
    }
    auto cleanup_writer = util::defer([a]() { archive_write_free(a); });
    auto fail = [&a](const std::string& msg) {
        std::string detail =
            archive_error_string(a) ? archive_error_string(a) : "unknown error";
        return util::unexpected<std::string>{
            fmt::format("{}: {}", msg, detail)};
    };

    // pax-restricted: plain ustar headers, falling back to pax extended headers
    // only when a field (e.g. a long path) does not fit. Deterministic and the
    // closest match to the previous `tar --format=posix` output.
    archive_write_set_format_pax_restricted(a);
    archive_write_set_bytes_per_block(a, 1);
    archive_write_set_bytes_in_last_block(a, 1);
    if (archive_write_open_filename(a, tar_path.c_str()) != ARCHIVE_OK) {
        return fail(fmt::format("unable to create {}", tar_path));
    }

    std::vector<char> buf(128 * 1024);
    for (const auto& p : paths) {
        struct stat st{};
        if (::lstat(p.c_str(), &st) != 0) {
            return util::unexpected{fmt::format("unable to stat {}", p)};
        }
        const bool is_dir = S_ISDIR(st.st_mode);
        const bool is_reg = S_ISREG(st.st_mode);
        if (!is_dir && !is_reg) {
            return util::unexpected{fmt::format(
                "unsupported file type (only regular files and directories are "
                "supported): {}",
                p)};
        }

        archive_entry* entry = archive_entry_new();
        auto cleanup_entry =
            util::defer([entry]() { archive_entry_free(entry); });
        const auto name = p.lexically_relative(base).string();
        archive_entry_set_pathname(entry, name.c_str());
        archive_entry_set_filetype(entry, is_dir ? AE_IFDIR : AE_IFREG);
        archive_entry_set_perm(entry, st.st_mode & 07777);
        archive_entry_set_size(entry, is_reg ? st.st_size : 0);
        // deterministic metadata: zeroed timestamps and numeric owner.
        archive_entry_set_mtime(entry, 0, 0);
        archive_entry_set_uid(entry, 0);
        archive_entry_set_gid(entry, 0);
        archive_entry_set_uname(entry, nullptr);
        archive_entry_set_gname(entry, nullptr);

        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            return fail(fmt::format("unable to write tar header for {}", name));
        }

        if (is_reg && st.st_size > 0) {
            std::FILE* f = std::fopen(p.c_str(), "rb");
            if (f == nullptr) {
                return util::unexpected{fmt::format("unable to read {}", p)};
            }
            auto cleanup_file = util::defer([f]() { std::fclose(f); });
            std::size_t n = 0;
            while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
                if (archive_write_data(a, buf.data(), n) < 0) {
                    return fail(
                        fmt::format("unable to write tar data for {}", name));
                }
            }
            if (std::ferror(f) != 0) {
                return util::unexpected{fmt::format("error reading {}", p)};
            }
        }
    }

    if (archive_write_close(a) != ARCHIVE_OK) {
        return fail("unable to finalize the tar");
    }
    return {};
}

// gzip `tar_path` into `gz_path`, returning the digest and size of the gzipped
// output. zlib's default gzip header carries mtime = 0 and no filename, i.e. it
// is equivalent to `gzip -n`, so the output is reproducible with no extra work.
util::expected<packed_targz, std::string>
gzip_file(const fs::path& tar_path, const fs::path& gz_path,
          const util::sha256& tar_sha) {
    std::FILE* in = std::fopen(tar_path.c_str(), "rb");
    if (in == nullptr) {
        return util::unexpected{fmt::format("unable to read {}", tar_path)};
    }
    auto cleanup_in = util::defer([in]() { std::fclose(in); });
    std::FILE* out = std::fopen(gz_path.c_str(), "wb");
    if (out == nullptr) {
        return util::unexpected{fmt::format("unable to create {}", gz_path)};
    }
    auto cleanup_out = util::defer([out]() { std::fclose(out); });

    z_stream zs{};
    // windowBits 15 + 16 selects the gzip wrapper (rather than raw/zlib).
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return util::unexpected{"unable to initialize gzip compression"};
    }
    auto cleanup_zs = util::defer([&zs]() { deflateEnd(&zs); });

    util::sha256_hasher hasher;
    std::uint64_t gz_size = 0;
    std::array<unsigned char, 128 * 1024> in_buf{};
    std::array<unsigned char, 128 * 1024> out_buf{};
    int flush = Z_NO_FLUSH;
    std::string error;

    do {
        zs.avail_in =
            static_cast<uInt>(std::fread(in_buf.data(), 1, in_buf.size(), in));
        if (std::ferror(in) != 0) {
            error = fmt::format("error reading {}", tar_path);
            break;
        }
        flush = std::feof(in) != 0 ? Z_FINISH : Z_NO_FLUSH;
        zs.next_in = in_buf.data();

        do {
            zs.avail_out = static_cast<uInt>(out_buf.size());
            zs.next_out = out_buf.data();
            deflate(&zs, flush);
            const std::size_t produced = out_buf.size() - zs.avail_out;
            if (produced > 0) {
                if (std::fwrite(out_buf.data(), 1, produced, out) != produced) {
                    error = fmt::format("error writing {}", gz_path);
                    break;
                }
                hasher.update(std::as_bytes(
                    std::span<const unsigned char>(out_buf.data(), produced)));
                gz_size += produced;
            }
        } while (zs.avail_out == 0 && error.empty());
    } while (flush != Z_FINISH && error.empty());

    if (!error.empty()) {
        return util::unexpected{error};
    }

    return packed_targz{.gz_path = gz_path,
                        .tar_sha256 = tar_sha,
                        .gz_sha256 = hasher.finalize(),
                        .gz_size = gz_size};
}

} // namespace

util::expected<packed_targz, std::string>
pack_directory_targz(const fs::path& dir, const fs::path& work_dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return util::unexpected{
            fmt::format("not a directory: {}", dir.string())};
    }
    const auto tar_path = work_dir / "payload.tar";
    const auto gz_path = work_dir / "payload.tar.gz";

    if (auto ok = write_tar(dir, tar_path); !ok) {
        return util::unexpected{ok.error()};
    }
    // digest the uncompressed tar exactly as a streaming reader consumes it.
    auto tar_sha = util::sha256_file(tar_path);
    if (!tar_sha) {
        return util::unexpected{tar_sha.error()};
    }
    auto packed = gzip_file(tar_path, gz_path, *tar_sha);
    if (!packed) {
        return util::unexpected{packed.error()};
    }
    fs::remove(tar_path, ec);
    return *packed;
}

util::expected<void, std::string> extract_targz(const fs::path& archive_path,
                                                const fs::path& dest) {
    const auto dest_norm = fs::weakly_canonical(dest).lexically_normal();

    archive* r = archive_read_new();
    if (r == nullptr) {
        return util::unexpected{"unable to allocate a tar reader"};
    }
    auto cleanup_reader = util::defer([r]() { archive_read_free(r); });
    // tar + gzip only: no other formats/filters are pulled in.
    archive_read_support_format_tar(r);
    archive_read_support_filter_gzip(r);

    auto fail = [&r](const std::string& msg) {
        std::string detail =
            archive_error_string(r) ? archive_error_string(r) : "unknown error";
        return util::unexpected<std::string>{
            fmt::format("{}: {}", msg, detail)};
    };

    if (archive_read_open_filename(r, archive_path.c_str(), 64 * 1024) !=
        ARCHIVE_OK) {
        return fail(fmt::format("unable to open {}", archive_path));
    }

    archive_entry* entry = nullptr;
    int rc = 0;
    while ((rc = archive_read_next_header(r, &entry)) == ARCHIVE_OK) {
        const char* raw = archive_entry_pathname(entry);
        if (raw == nullptr) {
            return fail("tar entry with no name");
        }
        const auto mode = archive_entry_filetype(entry);
        if (mode != AE_IFREG && mode != AE_IFDIR) {
            return util::unexpected{fmt::format(
                "refusing to extract unsupported entry type: {}", raw)};
        }

        // resolve the target inside dest and refuse anything that escapes it.
        const auto target = (dest_norm / raw).lexically_normal();
        if (target != dest_norm && !util::is_child(target, dest_norm)) {
            return util::unexpected{fmt::format(
                "refusing to extract entry outside the destination: {}", raw)};
        }

        std::error_code ec;
        if (mode == AE_IFDIR) {
            fs::create_directories(target, ec);
            if (ec) {
                return util::unexpected{fmt::format("unable to create {}: {}",
                                                    target, ec.message())};
            }
            continue;
        }

        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            return util::unexpected{fmt::format(
                "unable to create {}: {}", target.parent_path(), ec.message())};
        }
        std::FILE* f = std::fopen(target.c_str(), "wb");
        if (f == nullptr) {
            return util::unexpected{fmt::format("unable to create {}", target)};
        }
        auto cleanup_file = util::defer([f]() { std::fclose(f); });
        const void* block = nullptr;
        std::size_t len = 0;
        la_int64_t offset = 0;
        int drc = 0;
        while ((drc = archive_read_data_block(r, &block, &len, &offset)) ==
               ARCHIVE_OK) {
            if (std::fwrite(block, 1, len, f) != len) {
                return util::unexpected{
                    fmt::format("error writing {}", target.string())};
            }
        }
        if (drc != ARCHIVE_EOF) {
            return fail(fmt::format("error extracting {}", raw));
        }
    }
    if (rc != ARCHIVE_EOF) {
        return fail("error reading the tar");
    }
    return {};
}

} // namespace util
