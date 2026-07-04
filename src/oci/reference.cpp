#include <string>
#include <string_view>

#include <oci/parse.h>
#include <oci/reference.h>

namespace oci {

reference reference::tag(std::string tag) {
    return reference{std::move(tag)};
}

reference reference::digest(oci::digest d) {
    return reference{std::move(d)};
}

util::expected<reference, util::parse_error>
reference::parse(std::string_view text) {
    return oci::parse_reference(text);
}

std::string reference::string() const {
    return digest_ ? digest_->string() : tag_;
}

} // namespace oci
