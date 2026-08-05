#pragma once

#include <cstdint>
#include <filesystem>

#include <util/expected.h>
#include <util/sha.h>

namespace util {

// Result of packing a directory into a deterministic gzipped tar.
struct packed_targz {
    std::filesystem::path gz_path; // the gzipped blob on disk
    util::sha256 tar_sha256;       // digest of the UNCOMPRESSED tar stream
    util::sha256 gz_sha256;        // digest of the gzipped blob
    std::uint64_t gz_size = 0;     // size of the gzipped blob in bytes
};

// How pack_directory_targz names entries and treats symlinks. The defaults are
// what the OCI meta layer needs; the CLI's recipe upload overrides both.
struct pack_options {
    // false: entry names are relative to `dir.parent_path()`, so the archive
    //        has a top-level `dir.filename()` entry (e.g. "meta/...").
    // true:  names are relative to `dir` itself and no entry is written for
    //        `dir`, i.e. the archive holds the directory's *contents*
    //        (equivalent to `tar -C dir -cf - .`).
    bool strip_root = false;

    // false: a symlink is an error, so a produced archive always round-trips
    //        through extract_targz.
    // true:  symlinks are followed and the target is archived in their place
    //        (equivalent to `tar --dereference`). A symlink cycle is an error.
    bool dereference = false;
};

// Pack `dir` (recursively) into a deterministic gzipped tar written under
// `work_dir`, returning the gz path plus the digests/size the OCI meta layer
// needs. By default the archive entries are named relative to
// `dir.parent_path()`, so the top-level entry is `dir.filename()` (e.g.
// "meta/..."); see `pack_options` to change that.
//
// Determinism / oras compatibility (see archive.cpp for the details):
//   * entries are sorted by pathname;
//   * mtime = 0, uid = gid = 0, uname/gname cleared (numeric owner);
//   * the tar has NO trailing record padding past the two zero blocks, so the
//     digest a streaming tar reader computes matches `tar_sha256`;
//   * the gzip header carries no name and mtime = 0 (equivalent to `gzip -n`).
//
// Only regular files and directories are supported; any other entry type
// (device, fifo, ...) is an error, as is a symlink unless
// `pack_options::dereference` is set.
util::expected<packed_targz, std::string>
pack_directory_targz(const std::filesystem::path& dir,
                     const std::filesystem::path& work_dir,
                     const pack_options& opts = {});

// Safely extract a gzipped tar into the existing directory `dest`. Every
// entry's on-disk path is verified to stay inside `dest`; an entry that would
// escape (via `..`, an absolute path, ...) fails the extraction. Only regular
// files and directories are extracted; any other entry type is rejected.
util::expected<void, std::string>
extract_targz(const std::filesystem::path& archive,
              const std::filesystem::path& dest);

} // namespace util
