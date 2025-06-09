#pragma once

#include <memory>
#include <optional>
#include <string>

#include <uenv/repository.h>
#include <util/expected.h>

namespace uenv {

enum class registry_type {
    oci,    // Generic OCI registry (Docker Hub, etc.)
    zot,    // Zot registry implementation
    ghcr,   // GitHub Container Registry
    site    // Site-specific implementation (JFrog, etc.)
};

class registry_backend {
public:
    virtual ~registry_backend() = default;
    
    // Get listing of images in a namespace
    // Returns empty repository for registries that don't support search
    virtual util::expected<uenv::repository, std::string> 
        get_listing(const std::string& nspace) = 0;
    
    // Get the registry URL
    virtual std::string get_url() const = 0;
    
    // Whether this registry supports search/listing operations
    virtual bool supports_search() const = 0;
    
    // Get the registry type
    virtual registry_type get_type() const = 0;
};

// Factory function to create registry backends
std::unique_ptr<registry_backend> 
create_registry(const std::string& url, registry_type type);

// Parse registry type from string
util::expected<registry_type, std::string> 
parse_registry_type(const std::string& type_str);

// Convert registry type to string
std::string registry_type_to_string(registry_type type);

} // namespace uenv