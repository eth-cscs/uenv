#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <oci/digest.h>
#include <oci/parse.h>

namespace oci {

namespace {

bool is_lower_hex(std::string_view s) {
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

digest digest::from_sha256(const util::sha256_digest& d) {
    return digest{"sha256", d.hex()};
}

digest digest::sha256(std::string hex) {
    assert(hex.size() == 64 && is_lower_hex(hex) &&
           "digest::sha256 requires a 64-char lowercase-hex string");
    return digest{"sha256", std::move(hex)};
}

util::expected<digest, util::parse_error> digest::parse(std::string_view text) {
    return oci::parse_digest(text);
}

std::string digest::string() const {
    return algorithm_ + ":" + hex_;
}

} // namespace oci
