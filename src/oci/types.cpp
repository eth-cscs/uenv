#include <string>
#include <string_view>

#include <fmt/format.h>

#include <oci/parse.h>
#include <oci/types.h>
#include <util/expected.h>
#include <util/parse.h>

namespace oci {

//
// registry addressing helpers (pure)
//

util::expected<registry_location, util::parse_error>
split_registry(std::string_view configured_url) {
    // parse the configured value as a URL; the scheme, if any, is dropped and
    // https is always used for the base. any port is preserved on the base.
    auto u = parse_url(configured_url);
    if (!u) {
        return util::unexpected(u.error());
    }

    // the repository prefix is the URL path with its surrounding slashes
    // trimmed.
    std::string prefix = u->path;
    if (!prefix.empty() && prefix.front() == '/') {
        prefix.erase(prefix.begin());
    }
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }

    // preserve an explicit scheme (e.g. http:// for a local test registry);
    // default to https when the config omits one (the CSCS deployment).
    const std::string scheme = u->scheme.empty() ? "https" : u->scheme;
    std::string base = scheme + "://" + u->host;
    if (u->port) {
        base += ':' + std::to_string(*u->port);
    }
    return registry_location{.base = std::move(base),
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
