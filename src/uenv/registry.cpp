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
    get_listing(const std::string& nspace) const {
        spdlog::debug("OCI registry does not support listing for namespace: {}",
                      nspace);
        // Return empty repository - OCI registries generally don't support
        // search
        return uenv::create_repository();
    }

    std::string get_url() const {
        return url_;
    }

    bool supports_search() const {
        return false;
    }

    registry_type get_type() const {
        return registry_type::oci;
    }
};

// Zot Registry implementation
struct zot_registry {
  private:
    std::string url_;

  public:
    explicit zot_registry(const std::string& url) : url_(url) {
    }

    util::expected<uenv::repository, std::string>
    get_listing(const std::string& nspace) const {
        spdlog::debug("Zot registry listing for namespace: {}", nspace);
        // Zot supports some search capabilities, but for now return empty
        // This could be extended to use Zot's GraphQL API
        return uenv::create_repository();
    }

    std::string get_url() const {
        return url_;
    }

    bool supports_search() const {
        return false; // Could be true if we implement Zot's GraphQL API
    }

    registry_type get_type() const {
        return registry_type::zot;
    }
};

// GitHub Container Registry implementation
struct ghcr_registry {
  private:
    std::string url_;

  public:
    explicit ghcr_registry(const std::string& url) : url_(url) {
    }

    util::expected<uenv::repository, std::string>
    get_listing(const std::string& nspace) const {
        spdlog::debug(
            "GHCR registry does not support listing for namespace: {}", nspace);
        // GHCR doesn't support comprehensive search via OCI APIs
        return uenv::create_repository();
    }

    std::string get_url() const {
        return url_;
    }

    bool supports_search() const {
        return false;
    }

    registry_type get_type() const {
        return registry_type::ghcr;
    }
};

// Factory function implementation
registry create_registry(const std::string& url, registry_type type) {
    switch (type) {
    case registry_type::oci:
        return registry{oci_registry{url}};
    case registry_type::zot:
        return registry{zot_registry{url}};
    case registry_type::ghcr:
        return registry{ghcr_registry{url}};
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
parse_registry_type(const std::string& type_str) {
    if (type_str == "oci") {
        return registry_type::oci;
    } else if (type_str == "zot") {
        return registry_type::zot;
    } else if (type_str == "ghcr") {
        return registry_type::ghcr;
    } else if (type_str == "site") {
        return registry_type::site;
    } else {
        return util::unexpected(fmt::format(
            "Invalid registry type: {}. Valid types are: oci, zot, ghcr, site",
            type_str));
    }
}

// Convert registry type to string
std::string registry_type_to_string(registry_type type) {
    switch (type) {
    case registry_type::oci:
        return "oci";
    case registry_type::zot:
        return "zot";
    case registry_type::ghcr:
        return "ghcr";
    case registry_type::site:
        return "site";
    }
    return "unknown";
}

} // namespace uenv
