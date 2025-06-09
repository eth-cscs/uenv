#include "registry.h"

#include <spdlog/spdlog.h>

#include <uenv/repository.h>

namespace uenv {

// Generic OCI Registry implementation
class oci_registry : public registry_backend {
private:
    std::string url_;

public:
    explicit oci_registry(const std::string& url) : url_(url) {}

    util::expected<uenv::repository, std::string> 
    get_listing(const std::string& nspace) override {
        spdlog::debug("OCI registry does not support listing for namespace: {}", nspace);
        // Return empty repository - OCI registries generally don't support search
        return uenv::create_repository();
    }

    std::string get_url() const override {
        return url_;
    }

    bool supports_search() const override {
        return false;
    }

    registry_type get_type() const override {
        return registry_type::oci;
    }
};

// Zot Registry implementation
class zot_registry : public registry_backend {
private:
    std::string url_;

public:
    explicit zot_registry(const std::string& url) : url_(url) {}

    util::expected<uenv::repository, std::string> 
    get_listing(const std::string& nspace) override {
        spdlog::debug("Zot registry listing for namespace: {}", nspace);
        // Zot supports some search capabilities, but for now return empty
        // This could be extended to use Zot's GraphQL API
        return uenv::create_repository();
    }

    std::string get_url() const override {
        return url_;
    }

    bool supports_search() const override {
        return false; // Could be true if we implement Zot's GraphQL API
    }

    registry_type get_type() const override {
        return registry_type::zot;
    }
};

// GitHub Container Registry implementation
class ghcr_registry : public registry_backend {
private:
    std::string url_;

public:
    explicit ghcr_registry(const std::string& url) : url_(url) {}

    util::expected<uenv::repository, std::string> 
    get_listing(const std::string& nspace) override {
        spdlog::debug("GHCR registry does not support listing for namespace: {}", nspace);
        // GHCR doesn't support comprehensive search via OCI APIs
        return uenv::create_repository();
    }

    std::string get_url() const override {
        return url_;
    }

    bool supports_search() const override {
        return false;
    }

    registry_type get_type() const override {
        return registry_type::ghcr;
    }
};

// Factory function implementation
std::unique_ptr<registry_backend> 
create_registry(const std::string& url, registry_type type) {
    switch (type) {
        case registry_type::oci:
            return std::make_unique<oci_registry>(url);
        case registry_type::zot:
            return std::make_unique<zot_registry>(url);
        case registry_type::ghcr:
            return std::make_unique<ghcr_registry>(url);
        case registry_type::site:
            // Site-specific implementation will be created in site module
            spdlog::error("Site registry type should be created via site module");
            return nullptr;
    }
    return nullptr;
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
        return util::unexpected(
            "Invalid registry type: " + type_str + 
            ". Valid types are: oci, zot, ghcr, site");
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