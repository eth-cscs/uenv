#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <util/expected.h>

namespace util {

// A SHA-256 digest: 32 raw bytes, plus a canonical lowercase-hex rendering.
struct sha256_digest {
    std::array<std::uint8_t, 32> bytes;

    // 64-character lowercase hex string.
    std::string hex() const;

    friend bool operator==(const sha256_digest&,
                           const sha256_digest&) = default;
};

// --- low-level streaming interface --------------------------------------
//
// An opaque state blob you sha256_init() once, feed bytes into with
// sha256_update(), then sha256_final(). Stack-allocatable and trivially
// copyable; the internal layout lives entirely in sha256.cpp, so the FIPS
// implementation never leaks into this header. Use this when data must be
// digested incrementally (e.g. streaming a multi-GB squashfs) without holding
// it all in memory.
struct sha256_state {
    // opaque storage - do not inspect or modify directly. sized and aligned to
    // hold the implementation state (asserted in sha256.cpp).
    alignas(std::uint64_t) std::byte opaque[112];
};

void sha256_init(sha256_state& state);
void sha256_update(sha256_state& state, std::span<const std::byte> data);
sha256_digest sha256_final(sha256_state& state);

// --- convenience one-shots ----------------------------------------------

// digest an in-memory byte buffer.
sha256_digest sha256_bytes(std::span<const std::byte> data);

// digest text - a thin wrapper for the common string case.
sha256_digest sha256_string(std::string_view text);

// digest a file by streaming it in fixed-size chunks, so large files never
// need to be held in memory. returns an error message if the file is
// unreadable.
util::expected<sha256_digest, std::string>
sha256_file(const std::filesystem::path& path);

} // namespace util
