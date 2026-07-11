#include <string>
#include <string_view>

#include <oci/parse.h>
#include <oci/tag.h>

namespace oci {

util::expected<tag, util::parse_error> tag::parse(std::string_view text) {
    return oci::parse_tag(text);
}

const std::string& tag::string() const {
    return value_;
}

} // namespace oci
