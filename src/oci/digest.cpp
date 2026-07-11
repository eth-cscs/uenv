#include <string>
#include <string_view>

#include <oci/digest.h>
#include <oci/parse.h>

namespace oci {

digest digest::from_sha256(const util::sha256_digest& d) {
    return digest{"sha256", d.hex().string()};
}

digest digest::sha256(const util::sha256& hex) {
    return digest{"sha256", hex.string()};
}

util::expected<digest, util::parse_error> digest::parse(std::string_view text) {
    return oci::parse_digest(text);
}

const std::string& digest::algorithm() const {
    return algorithm_;
}

const std::string& digest::hex() const {
    return hex_;
}

std::string digest::string() const {
    return algorithm_ + ":" + hex_;
}

} // namespace oci
