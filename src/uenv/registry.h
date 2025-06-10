#pragma once

#include <memory>
#include <optional>
#include <string>

#include <uenv/repository.h>
#include <util/expected.h>

namespace uenv {

enum class registry_type {
    oci,  // Generic OCI registry (Docker Hub, etc.)
    zot,  // Zot registry implementation
    ghcr, // GitHub Container Registry
    site  // Site-specific implementation (JFrog, etc.)
};

// Concept for registry implementations
// Any type T that implements these operations can be used as a registry
template <typename T>
concept RegistryImpl = requires(T registry, const std::string& nspace) {
    {
        registry.get_listing(nspace)
    } -> std::convertible_to<util::expected<uenv::repository, std::string>>;
    { registry.get_url() } -> std::convertible_to<std::string>;
    { registry.supports_search() } -> std::convertible_to<bool>;
    { registry.get_type() } -> std::convertible_to<registry_type>;
};

// Type-erased registry class using value semantics
class registry {
  public:
    template <RegistryImpl T>
    registry(T impl) : impl_(std::make_unique<wrap<T>>(std::move(impl))) {
    }

    registry(registry&& other) = default;

    registry(const registry& other) : impl_(other.impl_->clone()) {
    }

    registry& operator=(registry&& other) = default;
    registry& operator=(const registry& other) {
        return *this = registry(other);
    }

    // Get listing of images in a namespace
    // Returns empty repository for registries that don't support search
    util::expected<uenv::repository, std::string>
    get_listing(const std::string& nspace) const {
        return impl_->get_listing(nspace);
    }

    // Get the registry URL
    std::string get_url() const {
        return impl_->get_url();
    }

    // Whether this registry supports search/listing operations
    bool supports_search() const {
        return impl_->supports_search();
    }

    // Get the registry type
    registry_type get_type() const {
        return impl_->get_type();
    }

  private:
    struct interface {
        virtual ~interface() = default;
        virtual std::unique_ptr<interface> clone() = 0;
        virtual util::expected<uenv::repository, std::string>
        get_listing(const std::string& nspace) const = 0;
        virtual std::string get_url() const = 0;
        virtual bool supports_search() const = 0;
        virtual registry_type get_type() const = 0;
    };

    std::unique_ptr<interface> impl_;

    template <RegistryImpl T> struct wrap : interface {
        explicit wrap(const T& impl) : wrapped(impl) {
        }
        explicit wrap(T&& impl) : wrapped(std::move(impl)) {
        }

        virtual std::unique_ptr<interface> clone() override {
            return std::make_unique<wrap<T>>(wrapped);
        }

        virtual util::expected<uenv::repository, std::string>
        get_listing(const std::string& nspace) const override {
            return wrapped.get_listing(nspace);
        }

        virtual std::string get_url() const override {
            return wrapped.get_url();
        }

        virtual bool supports_search() const override {
            return wrapped.supports_search();
        }

        virtual registry_type get_type() const override {
            return wrapped.get_type();
        }

        T wrapped;
    };
};

// Factory function to create registry implementations
registry create_registry(const std::string& url, registry_type type);

// Parse registry type from string
util::expected<registry_type, std::string>
parse_registry_type(const std::string& type_str);

// Convert registry type to string
std::string registry_type_to_string(registry_type type);

} // namespace uenv
