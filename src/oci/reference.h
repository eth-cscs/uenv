#pragma once

#include <string>
#include <string_view>
#include <variant>

#include <oci/digest.h>
#include <oci/tag.h>
#include <util/expected.h>
#include <util/parse.h>

namespace oci {

// A manifest reference: either a mutable tag or an immutable content digest.
// Both occupy the same slot in the registry path (/manifests/<reference>), but
// they are different concepts — a tag can move, a digest is content-addressed
// and self-verifying — so they are kept distinct at the type level.
class reference {
  public:
    // build a reference from a tag.
    static reference tag(oci::tag t);
    // build a reference from a content digest.
    static reference digest(oci::digest d);
    // parse text: a valid "<algo>:<hex>" becomes a digest reference, otherwise
    // it is validated against the OCI tag grammar and becomes a tag reference.
    // a thin forwarder to oci::parse_reference.
    static util::expected<reference, util::parse_error>
    parse(std::string_view text);

    bool is_digest() const;
    bool is_tag() const;
    // the tag, if this reference is a tag.
    const oci::tag& as_tag() const;
    // the digest, if this reference is a digest.
    const oci::digest& as_digest() const;
    // the text that goes into the registry path (the tag, or "<algo>:<hex>").
    std::string string() const;

    friend bool operator==(const reference&, const reference&) = default;

  private:
    explicit reference(oci::tag t) : value_(std::move(t)) {
    }
    explicit reference(oci::digest d) : value_(std::move(d)) {
    }
    std::variant<oci::tag, oci::digest> value_;
};

} // namespace oci
