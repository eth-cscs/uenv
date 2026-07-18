#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <archive.h>
#include <archive_entry.h>
#include <catch2/catch_all.hpp>
#include <zlib.h>

#include <util/archive.h>
#include <util/fs.h>
#include <util/sha.h>

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& p, const std::string& contents) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// build a small directory tree with nested subdirectories under a fresh temp
// dir, returning the path of the top directory to be packed.
fs::path make_tree() {
    auto root = *util::make_temp_dir();
    auto dir = root / "meta";
    write_file(dir / "env.json", "{\"name\":\"tool\"}");
    write_file(dir / "recipe" / "config.yaml", "store: /user-environment\n");
    write_file(dir / "recipe" / "extra" / "reframe.yaml", "systems: []\n");
    write_file(dir / "extra" / "notes.txt", "hello");
    return dir;
}

// gunzip a file entirely into memory using zlib (windowBits 15+16 = gzip).
std::vector<unsigned char> gunzip(const fs::path& gz) {
    std::string in = read_file(gz);
    std::vector<unsigned char> out;
    z_stream zs{};
    REQUIRE(inflateInit2(&zs, 15 + 16) == Z_OK);
    zs.next_in = reinterpret_cast<unsigned char*>(in.data());
    zs.avail_in = static_cast<uInt>(in.size());
    std::vector<unsigned char> buf(64 * 1024);
    int rc = Z_OK;
    do {
        zs.next_out = buf.data();
        zs.avail_out = static_cast<uInt>(buf.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        REQUIRE((rc == Z_OK || rc == Z_STREAM_END));
        out.insert(out.end(), buf.data(),
                   buf.data() + (buf.size() - zs.avail_out));
    } while (rc != Z_STREAM_END);
    inflateEnd(&zs);
    return out;
}

// write a single-entry gzipped tar with an arbitrary (possibly hostile)
// pathname, so extraction hardening can be exercised.
void write_evil_targz(const fs::path& gz, const std::string& entry_name,
                      int filetype, const std::string& contents) {
    archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_add_filter_gzip(a);
    REQUIRE(archive_write_open_filename(a, gz.c_str()) == ARCHIVE_OK);
    archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, entry_name.c_str());
    archive_entry_set_filetype(e, filetype);
    archive_entry_set_perm(e, 0644);
    if (filetype == AE_IFLNK) {
        archive_entry_set_symlink(e, "/etc/passwd");
    } else {
        archive_entry_set_size(e, static_cast<la_int64_t>(contents.size()));
    }
    REQUIRE(archive_write_header(a, e) == ARCHIVE_OK);
    if (filetype == AE_IFREG && !contents.empty()) {
        archive_write_data(a, contents.data(), contents.size());
    }
    archive_entry_free(e);
    archive_write_close(a);
    archive_write_free(a);
}

} // namespace

TEST_CASE("pack_directory_targz round-trips through extract_targz",
          "[archive]") {
    auto dir = make_tree();
    auto work = *util::make_temp_dir();

    auto packed = util::pack_directory_targz(dir, work);
    REQUIRE(packed);
    REQUIRE(fs::exists(packed->gz_path));

    auto dest = *util::make_temp_dir();
    auto ok = util::extract_targz(packed->gz_path, dest);
    REQUIRE(ok);

    // the top-level entry name is the packed directory's own name ("meta").
    auto out = dest / "meta";
    REQUIRE(read_file(out / "env.json") == read_file(dir / "env.json"));
    REQUIRE(read_file(out / "recipe" / "config.yaml") ==
            read_file(dir / "recipe" / "config.yaml"));
    REQUIRE(read_file(out / "recipe" / "extra" / "reframe.yaml") ==
            read_file(dir / "recipe" / "extra" / "reframe.yaml"));
    REQUIRE(read_file(out / "extra" / "notes.txt") ==
            read_file(dir / "extra" / "notes.txt"));
}

TEST_CASE("pack_directory_targz has no padding past the tar EOF marker",
          "[archive]") {
    auto dir = make_tree();
    auto work = *util::make_temp_dir();
    auto packed = util::pack_directory_targz(dir, work);
    REQUIRE(packed);

    auto tar = gunzip(packed->gz_path);
    const std::size_t n = tar.size();

    // a well-formed tar is a whole number of 512-byte blocks and ends with two
    // all-zero blocks (the end-of-archive marker).
    REQUIRE(n % 512 == 0);
    REQUIRE(n >= 1024);
    const bool last_two_zero = std::all_of(
        tar.end() - 1024, tar.end(), [](unsigned char c) { return c == 0; });
    REQUIRE(last_two_zero);
    // the block *before* the marker must carry real data: this is what fails if
    // GNU-tar-style record padding sneaks back in (oras would then hash fewer
    // bytes than the recorded content digest and report a mismatch).
    const bool third_last_nonzero =
        !std::all_of(tar.end() - 1536, tar.end() - 1024,
                     [](unsigned char c) { return c == 0; });
    REQUIRE(third_last_nonzero);

    // the recorded uncompressed-tar digest must equal the digest of the full
    // stream a reader consumes.
    auto full = util::sha256_bytes(std::as_bytes(std::span(tar)));
    REQUIRE(full == packed->tar_sha256);

    // the recorded gz digest and size must match the blob on disk.
    auto gz = util::sha256_file(packed->gz_path);
    REQUIRE(gz);
    REQUIRE(*gz == packed->gz_sha256);
    REQUIRE(fs::file_size(packed->gz_path) == packed->gz_size);
}

TEST_CASE("pack_directory_targz is deterministic", "[archive]") {
    auto dir = make_tree();
    auto p1 = util::pack_directory_targz(dir, *util::make_temp_dir());
    auto p2 = util::pack_directory_targz(dir, *util::make_temp_dir());
    REQUIRE(p1);
    REQUIRE(p2);
    REQUIRE(p1->tar_sha256 == p2->tar_sha256);
    REQUIRE(p1->gz_sha256 == p2->gz_sha256);
}

TEST_CASE("extract_targz rejects path traversal and unsupported entries",
          "[archive]") {
    auto dest = *util::make_temp_dir();

    SECTION("relative escape") {
        auto gz = *util::make_temp_dir() / "evil.tar.gz";
        write_evil_targz(gz, "../escape.txt", AE_IFREG, "pwned");
        auto ok = util::extract_targz(gz, dest);
        REQUIRE(!ok);
        REQUIRE(!fs::exists(dest.parent_path() / "escape.txt"));
    }

    SECTION("absolute path") {
        auto gz = *util::make_temp_dir() / "evil.tar.gz";
        write_evil_targz(gz, "/tmp/uenv-archive-escape.txt", AE_IFREG, "pwned");
        auto ok = util::extract_targz(gz, dest);
        REQUIRE(!ok);
        REQUIRE(!fs::exists("/tmp/uenv-archive-escape.txt"));
    }

    SECTION("symlink entry") {
        auto gz = *util::make_temp_dir() / "evil.tar.gz";
        write_evil_targz(gz, "link", AE_IFLNK, "");
        auto ok = util::extract_targz(gz, dest);
        REQUIRE(!ok);
        REQUIRE(!fs::exists(dest / "link"));
    }
}
