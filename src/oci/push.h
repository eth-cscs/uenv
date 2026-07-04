#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <oci/auth.h>
#include <oci/client.h>
#include <oci/digest.h>
#include <oci/reference.h>
#include <util/expected.h>

namespace oci {

// Push a squashfs image as an OCI artifact (the native replacement for
// `oras push --artifact-type application/x-squashfs`). Uploads the squashfs
// blob and the empty config, builds an oras-compatible image manifest with the
// layer titled "store.squashfs", and PUTs it under `ref` (a tag). Returns the
// manifest digest — the canonical image id.
util::expected<digest, std::string>
push_squashfs(client& c, const std::filesystem::path& squashfs,
              const reference& ref);

// Attach typed side-data to an existing image as an OCI referrer (the native
// replacement for `oras attach`). `subject` identifies the target image (tag or
// digest); `artifact_type` is e.g. "uenv/meta"; `payload` is a file or a
// directory (a directory is packed as a deterministic gzipped tar flagged for
// unpacking, matching today's uenv/meta layout). Returns the descriptor of the
// pushed referrer manifest.
util::expected<descriptor, std::string>
attach(client& c, const reference& subject, std::string_view artifact_type,
       const std::filesystem::path& payload);

// Copy an image (and its referrers) from `src_repo` to `dst_repo` within the
// same registry (the native replacement for `oras cp --recursive`). Blobs are
// moved by cross-repo mount where the registry allows it, else streamed through
// local disk. The destination image is tagged `dst_tag`. Manifests are copied
// byte-for-byte, so the image digest (identity) is preserved.
util::expected<void, std::string>
copy_image(const std::string& registry_base, const std::string& src_repo,
           const std::string& dst_repo, const digest& src_manifest,
           const std::string& dst_tag, std::optional<credentials> creds);

} // namespace oci
