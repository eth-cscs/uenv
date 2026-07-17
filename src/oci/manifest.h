#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <oci/digest.h>
#include <oci/types.h>
#include <util/expected.h>

namespace oci {

// --- media types + the canonical empty config --------------------------------

// oras-style artifact manifests carry an "empty" JSON config object. This is
// the well-known descriptor for the two-byte body "{}".
inline constexpr std::string_view media_type_empty =
    "application/vnd.oci.empty.v1+json";
inline constexpr std::string_view empty_config_body = "{}";
// the sha-256 of "{}" and its base64 encoding.
inline constexpr std::string_view empty_config_hex =
    "44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a";
inline constexpr std::string_view empty_config_data = "e30=";

// layer media types used by uenv artifacts.
inline constexpr std::string_view media_type_layer_tar =
    "application/vnd.oci.image.layer.v1.tar";
inline constexpr std::string_view media_type_layer_targz =
    "application/vnd.oci.image.layer.v1.tar+gzip";

// artifact types.
inline constexpr std::string_view artifact_type_squashfs =
    "application/x-squashfs";
inline constexpr std::string_view artifact_type_meta = "uenv/meta";

// annotation keys.
inline constexpr std::string_view annotation_title =
    "org.opencontainers.image.title";
inline constexpr std::string_view annotation_created =
    "org.opencontainers.image.created";
inline constexpr std::string_view annotation_unpack =
    "io.deis.oras.content.unpack";
inline constexpr std::string_view annotation_content_digest =
    "io.deis.oras.content.digest";

// the canonical empty-config descriptor (inline "{}" with base64 `data`).
descriptor empty_config_descriptor();

// --- manifest model ----------------------------------------------------------

// A layer entry in an image manifest: a blob descriptor plus its annotations.
// annotations are held in a sorted map so serialization is deterministic and
// matches oras (which serializes Go maps with sorted keys).
struct manifest_layer {
    std::string media_type;
    oci::digest digest;
    std::size_t size = 0;
    std::map<std::string, std::string> annotations;

    // value of org.opencontainers.image.title, if present.
    std::optional<std::string> title() const;
    // true if annotated io.deis.oras.content.unpack == "true".
    bool wants_unpack() const;
};

// A full OCI image manifest (application/vnd.oci.image.manifest.v1+json). This
// is both the input to serialize_manifest (write) and the result of
// parse_manifest (read). New instances default to the empty config, since that
// is what every artifact uenv writes uses.
struct manifest {
    std::string media_type = std::string{media_type_manifest};
    std::string artifact_type;
    descriptor config = empty_config_descriptor();
    std::vector<manifest_layer> layers;
    std::optional<descriptor> subject;
    std::map<std::string, std::string> annotations;

    // find the layer with org.opencontainers.image.title == title, or nullptr.
    const manifest_layer* find_layer_by_title(std::string_view title) const;
    // find the first layer marked for unpacking, or nullptr.
    const manifest_layer* find_unpack_layer() const;
};

// Serialize `m` to a compact OCI image-manifest JSON document, byte-for-byte in
// the same shape oras produces: schemaVersion, mediaType, artifactType, config,
// layers, optional subject, annotations.
std::string serialize_manifest(const manifest& m);

// Parse an OCI image-manifest JSON document. Fails if the JSON is malformed or
// a descriptor digest is invalid.
util::expected<manifest, std::string> parse_manifest(std::string_view body);

// Serialize an OCI image index (application/vnd.oci.image.index.v1+json)
// listing `manifests`. Used for the referrers-tag fallback on registries that
// do not implement the Referrers API.
std::string serialize_index(const std::vector<descriptor>& manifests);

} // namespace oci
