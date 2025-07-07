#include "registry.h"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <uenv/repository.h>

namespace uenv {

// Generic OCI Registry implementation
struct oci_registry {
  private:
    std::string url_;

  public:
    explicit oci_registry(const std::string& url) : url_(url) {
    }

    util::expected<uenv::repository, std::string>
    listing(const std::string& nspace) const {
        spdlog::debug("OCI registry does not support listing for namespace: {}",
                      nspace);
        // Return empty repository - OCI registries generally don't support
        // search
        return uenv::create_repository();
    }

    std::string url() const {
        return url_;
    }

    bool supports_search() const {
        return false;
    }

    registry_type type() const {
        return registry_type::oci;
    }

    util::expected<uenv::manifest, std::string>
    manifest(const std::string&, const uenv::uenv_label&) const {
        // TODO: fill this out
        return {};
    }
};

// Factory function implementation
registry create_registry(const std::string& url, registry_type type) {
    spdlog::debug("creating registry {}::{}", type, url);
    switch (type) {
    case registry_type::oci:
        return registry{oci_registry{url}};
    case registry_type::site:
        // Site-specific implementation will be created in site module
        spdlog::error("Site registry type should be created via site module");
        // Return a default OCI registry as fallback
        return registry{oci_registry{url}};
    }
    // Should never reach here, but provide a fallback
    return registry{oci_registry{url}};
}

// Parse registry type from string
util::expected<registry_type, std::string>
parse_registry_type(const std::string& type) {
    if (type == "oci") {
        return registry_type::oci;
    } else if (type == "site") {
        return registry_type::site;
    } else {
        return util::unexpected(fmt::format(
            "Invalid registry type: {}. Valid types are: oci, site", type));
    }
}

} // namespace uenv
