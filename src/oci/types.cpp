#include <string>
#include <string_view>

#include <fmt/format.h>

#include <oci/parse.h>
#include <oci/types.h>
#include <util/expected.h>
#include <util/parse.h>
#include <util/url.h>

namespace oci {

//
// registry addressing helpers (pure)
//

registry_location split_registry(const util::url& configured_url) {
    // the repository prefix is the url path with its surrounding slashes
    // trimmed.
    std::string prefix = configured_url.path();
    if (!prefix.empty() && prefix.front() == '/') {
        prefix.erase(prefix.begin());
    }
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }

    return registry_location{.base = configured_url.origin(),
                             .prefix = std::move(prefix)};
}

std::string repository_path(std::string_view prefix, std::string_view nspace,
                            std::string_view system, std::string_view uarch,
                            std::string_view name, std::string_view version) {
    if (prefix.empty()) {
        return fmt::format("{}/{}/{}/{}/{}", nspace, system, uarch, name,
                           version);
    }
    return fmt::format("{}/{}/{}/{}/{}/{}", prefix, nspace, system, uarch, name,
                       version);
}

} // namespace oci
