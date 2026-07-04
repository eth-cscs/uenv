#pragma once

#include <string>
#include <string_view>

#include <util/expected.h>
#include <util/parse.h>
#include <util/sha256.h>

namespace oci {

class digest;
// the lexer-based parser (src/oci/parse.cpp) constructs digests directly.
util::expected<digest, util::parse_error> parse_digest(std::string_view);

// An OCI content digest: an algorithm plus a lowercase-hex value, rendered as
// "<algorithm>:<hex>" (e.g. "sha256:abc..."). A `digest` is always
// syntactically valid — it can only be obtained by parsing (which validates the
// algorithm and hex) or by promoting a computed hash — so callers never juggle
// bare hex against prefixed strings or a tag against a digest.
class digest {
  public:
    // promote a computed sha-256 hash to a digest.
    static digest from_sha256(const util::sha256_digest& d);

    // build a sha-256 digest from a known-valid 64-char lowercase-hex string
    // (e.g. a uenv record's sha). the precondition is asserted; use parse() for
    // input that might be malformed.
    static digest sha256(std::string hex);

    // parse "<algorithm>:<hex>". rejects an unknown algorithm, and a value whose
    // length does not match the algorithm or that is not lowercase hex. a thin
    // forwarder to oci::parse_digest.
    static util::expected<digest, util::parse_error> parse(std::string_view text);

    const std::string& algorithm() const {
        return algorithm_;
    }
    const std::string& hex() const {
        return hex_;
    }
    // the canonical "<algorithm>:<hex>" string.
    std::string string() const;

    friend bool operator==(const digest&, const digest&) = default;
    friend util::expected<digest, util::parse_error>
    parse_digest(std::string_view);

  private:
    digest(std::string algorithm, std::string hex)
        : algorithm_(std::move(algorithm)), hex_(std::move(hex)) {
    }
    std::string algorithm_;
    std::string hex_;
};

} // namespace oci

#include <fmt/core.h>
template <> class fmt::formatter<oci::digest> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    template <typename FmtContext>
    auto format(oci::digest const& d, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", d.string());
    }
};
