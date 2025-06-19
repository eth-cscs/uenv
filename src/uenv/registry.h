#pragma once

#include <memory>
#include <optional>
#include <string>

#include <fmt/core.h>

#include <uenv/repository.h>
#include <uenv/uenv.h>
#include <util/expected.h>

namespace uenv {

struct manifest {
    sha256 digest;
    sha256 squashfs_digest;
    size_t squashfs_bytes;
    std::optional<sha256> meta_digest;
    std::string respository;
    std::string tag;
};

enum class registry_type {
    oci, // Generic OCI registry (Docker Hub, etc.)
    site // Site-specific implementation (JFrog, etc.)
};

// Concept for registry implementations
// Any type T that implements these operations can be used as a registry
template <typename T>
concept RegistryImpl = requires(T registry, const std::string& nspace,
                                const uenv::uenv_label& label) {
    {
        registry.listing(nspace)
    } -> std::convertible_to<util::expected<uenv::repository, std::string>>;
    { registry.url() } -> std::convertible_to<std::string>;
    { registry.supports_search() } -> std::convertible_to<bool>;
    { registry.type() } -> std::convertible_to<registry_type>;
    {
        registry.manifest(nspace, label)
    } -> std::convertible_to<util::expected<uenv::manifest, std::string>>;
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
    listing(const std::string& nspace) const {
        return impl_->listing(nspace);
    }

    std::string url() const {
        return impl_->url();
    }

    bool supports_search() const {
        return impl_->supports_search();
    }

    registry_type type() const {
        return impl_->type();
    }

    util::expected<uenv::manifest, std::string>
    manifest(const std::string& nspace, const uenv_label& label) const {
        return impl_->manifest(nspace, label);
    }

  private:
    struct interface {
        virtual ~interface() = default;
        virtual std::unique_ptr<interface> clone() = 0;
        virtual util::expected<uenv::repository, std::string>
        listing(const std::string& nspace) const = 0;
        virtual std::string url() const = 0;
        virtual bool supports_search() const = 0;
        virtual registry_type type() const = 0;
        virtual util::expected<uenv::manifest, std::string>
        manifest(const std::string& nspace, const uenv_label& label) const = 0;
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
        listing(const std::string& nspace) const override {
            return wrapped.listing(nspace);
        }

        virtual std::string url() const override {
            return wrapped.url();
        }

        virtual bool supports_search() const override {
            return wrapped.supports_search();
        }

        virtual registry_type type() const override {
            return wrapped.type();
        }

        virtual util::expected<uenv::manifest, std::string>
        manifest(const std::string& nspace,
                 const uenv_label& label) const override {
            return wrapped.manifest(nspace, label);
        }

        T wrapped;
    };
};

// Factory function to create registry implementations
registry create_registry(const std::string& url, registry_type type);

// Parse registry type from string
util::expected<registry_type, std::string>
parse_registry_type(const std::string& type_str);

} // namespace uenv

template <> class fmt::formatter<uenv::registry_type> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::registry_type const& type,
                          FmtContext& ctx) const {
        using uenv::registry_type;

        switch (type) {
        case registry_type::oci:
            return fmt::format_to(ctx.out(), "oci");
        case registry_type::site:
            return fmt::format_to(ctx.out(), "site");
        }
        return fmt::format_to(ctx.out(), "unknown");
    }
};
