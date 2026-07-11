#include <string>
#include <string_view>
#include <variant>

#include <oci/parse.h>
#include <oci/reference.h>

namespace oci {

reference reference::tag(oci::tag t) {
    return reference{std::move(t)};
}

reference reference::digest(oci::digest d) {
    return reference{std::move(d)};
}

util::expected<reference, util::parse_error>
reference::parse(std::string_view text) {
    return oci::parse_reference(text);
}

bool reference::is_digest() const {
    return std::holds_alternative<oci::digest>(value_);
}

bool reference::is_tag() const {
    return std::holds_alternative<oci::tag>(value_);
}

const oci::tag& reference::as_tag() const {
    return std::get<oci::tag>(value_);
}

const oci::digest& reference::as_digest() const {
    return std::get<oci::digest>(value_);
}

std::string reference::string() const {
    return is_tag() ? as_tag().string() : as_digest().string();
}

} // namespace oci
