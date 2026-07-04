#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <oci/digest.h>
#include <util/expected.h>
#include <util/parse.h>

namespace oci {

// A manifest reference: either a mutable tag or an immutable content digest.
// Both occupy the same slot in the registry path (/manifests/<reference>), but
// they are different concepts — a tag can move, a digest is content-addressed
// and self-verifying — so they are kept distinct at the type level.
class reference {
  public:
    // build a reference from a tag. the tag is trusted (use parse() to validate
    // untrusted input).
    static reference tag(std::string tag);
    // build a reference from a content digest.
    static reference digest(oci::digest d);
    // parse text: a valid "<algo>:<hex>" becomes a digest reference, otherwise
    // it is validated against the OCI tag grammar and becomes a tag reference. a
    // thin forwarder to oci::parse_reference.
    static util::expected<reference, util::parse_error>
    parse(std::string_view text);

    bool is_digest() const {
        return digest_.has_value();
    }
    bool is_tag() const {
        return !digest_.has_value();
    }
    // the digest, if this reference is a digest.
    const std::optional<oci::digest>& as_digest() const {
        return digest_;
    }
    // the text that goes into the registry path (the tag, or "<algo>:<hex>").
    std::string string() const;

    friend bool operator==(const reference&, const reference&) = default;

  private:
    explicit reference(std::string tag) : tag_(std::move(tag)) {
    }
    explicit reference(oci::digest d) : digest_(std::move(d)) {
    }
    std::string tag_;
    std::optional<oci::digest> digest_;
};

} // namespace oci
