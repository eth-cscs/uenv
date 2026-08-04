#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <util/expected.h>

namespace util {

// true if every character of s is a lowercase hex digit (0-9, a-f). the single
// source of truth for hex validation across the codebase.
bool is_sha_string(std::string_view s);

// A fixed-length lowercase-hex string of exactly N characters, e.g. a content
// sha (N=64) or its short id (N=16). A sha_type is always valid once
// constructed: the only door in for untrusted input is parse(), which validates
// and returns expected, so constructing a sha_type never throws.
template <unsigned N> class sha_type {
  public:
    // a valid all-zero value; lets sha_type be a default-constructible member
    // (e.g. of uenv_record).
    sha_type() {
        value_.fill('0');
    }

    // parse exactly N lowercase-hex characters.
    static util::expected<sha_type, std::string> parse(std::string_view text) {
        if (text.size() != N) {
            return util::unexpected(
                fmt::format("'{}' is not a valid {}-character sha", text, N));
        }
        if (!is_sha_string(text)) {
            return util::unexpected(
                fmt::format("'{}' is not a lowercase-hex sha", text));
        }
        return sha_type{text};
    }

    std::string string() const {
        return std::string(value_.begin(), value_.end());
    }

    std::size_t hash() const {
        return std::hash<std::string_view>{}(
            std::string_view{value_.data(), value_.size()});
    }

    friend bool operator==(const sha_type&, const sha_type&) = default;
    friend auto operator<=>(const sha_type&, const sha_type&) = default;

  private:
    // trusted: the caller guarantees s is exactly N lowercase-hex characters.
    explicit sha_type(std::string_view s) {
        std::copy_n(s.begin(), N, value_.begin());
    }
    std::array<char, N> value_;
};

using sha256 = sha_type<64>;
using uenv_id = sha_type<16>;

//
// SHA-256 hashing
//

// forward-declared pimpl (in the style of util/lex.h's lexer_impl); the digest
// state never leaks into this header.
class sha256_impl;

// A streaming SHA-256 hasher. Construct it, feed bytes with update(), then call
// finalize() to obtain the digest. Use this when data must be digested
// incrementally (e.g. streaming a multi-GB squashfs to disk) without holding it
// all in memory.
class sha256_hasher {
  public:
    sha256_hasher();
    ~sha256_hasher();
    sha256_hasher(sha256_hasher&&) noexcept;
    sha256_hasher& operator=(sha256_hasher&&) noexcept;

    // feed the next chunk of input.
    void update(std::span<const std::byte> data);

    // restart from empty, discarding anything fed so far (e.g. a download that
    // must be retried from scratch).
    void reset();

    // finish and return the 64-character lowercase-hex digest. non-destructive:
    // the hasher may continue to be updated afterwards.
    sha256 finalize() const;

  private:
    std::unique_ptr<sha256_impl> impl_;
};

//
// convenience one-shots
//

// digest an in-memory byte buffer.
sha256 sha256_bytes(std::span<const std::byte> data);

// digest text - a thin wrapper for the common string case.
sha256 sha256_string(std::string_view text);

// digest a file by streaming it in fixed-size chunks, so large files never need
// to be held in memory. returns an error message if the file is unreadable.
// `progress`, when set, is called once per chunk with the cumulative number of
// bytes hashed so far, so that callers can drive a progress bar over a multi-GB
// file. It is called from this thread, and the hash is not abortable.
util::expected<sha256, std::string>
sha256_file(const std::filesystem::path& path,
            std::function<void(std::uint64_t bytes_hashed)> progress = {});

} // namespace util

template <unsigned N> class fmt::formatter<util::sha_type<N>> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    template <typename FmtContext>
    auto format(util::sha_type<N> const& sha, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", sha.string());
    }
};

namespace std {

// std::hash specialisation so sha_type can be used as an unordered-map key.
template <unsigned N> struct hash<util::sha_type<N>> {
    std::size_t operator()(const util::sha_type<N>& s) const noexcept {
        return s.hash();
    }
};

} // namespace std
