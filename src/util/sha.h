#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <util/expected.h>

namespace util {

// true if every character of s is a lowercase hex digit (0-9, a-f). the single
// source of truth for hex validation across the codebase.
bool is_sha_string(std::string_view s);

// forward declaration: sha256_digest (util/sha256.h) is the raw-bytes form of a
// computed hash, and its hex() promotes those bytes to a sha_type<64>. it is a
// friend so it can use the trusted constructor.
struct sha256_digest;

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

    friend struct sha256_digest;
};

using sha256 = sha_type<64>;
using uenv_id = sha_type<16>;

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
