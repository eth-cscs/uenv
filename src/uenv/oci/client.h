#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <uenv/oci/auth.h>
#include <util/expected.h>
#include <util/sha256.h>

namespace uenv {
namespace oci {

// common OCI media types
inline constexpr std::string_view media_type_manifest =
    "application/vnd.oci.image.manifest.v1+json";
inline constexpr std::string_view media_type_index =
    "application/vnd.oci.image.index.v1+json";
inline constexpr std::string_view media_type_octet_stream =
    "application/octet-stream";

// An OCI content descriptor (a manifest entry / referrer record).
struct descriptor {
    std::string media_type;
    std::string digest; // "sha256:<hex>"
    std::size_t size = 0;
    std::optional<std::string> artifact_type;

    friend bool operator==(const descriptor&, const descriptor&) = default;
};

// The result of fetching a manifest: the raw bytes plus the registry-reported
// digest and media type. The bytes are what must be re-digested locally to
// confirm identity.
struct manifest_response {
    std::string body;
    std::string digest; // value of the Docker-Content-Digest header
    std::string media_type;
};

// --- pure helpers (no network; unit-tested) -----------------------------

// "sha256:<hex>" for a digest.
std::string digest_string(const util::sha256_digest& d);

// OCI Distribution v2 path builders (each begins with "/v2/").
std::string blob_path(std::string_view repository, std::string_view digest);
std::string manifest_path(std::string_view repository,
                          std::string_view reference);
std::string uploads_path(std::string_view repository);
std::string tags_path(std::string_view repository);
std::string referrers_path(std::string_view repository,
                           std::string_view digest);

// Resolve a blob-upload Location (which may be absolute or registry-relative)
// against the registry base URL, then append the monolithic-upload digest query
// parameter, choosing '?' or '&' as appropriate.
std::string resolve_upload_url(std::string_view registry_url,
                               std::string_view location,
                               std::string_view digest);

// Parse a /tags/list body ({"name":...,"tags":[...]}) into the tag list.
std::optional<std::vector<std::string>>
parse_tags_list(std::string_view body);

// Parse a referrers index body into its manifest descriptors.
std::optional<std::vector<descriptor>> parse_referrers(std::string_view body);

// --- registry client ----------------------------------------------------

// A client bound to one repository on one registry. Authenticates lazily,
// caching a pull token and (separately) a pull,push token as operations need
// them. Read operations work anonymously when no credentials are supplied.
class client {
  public:
    // Probe the registry, parse its auth challenge, and bind to `repository`
    // (e.g. "deploy/todi/gh200/app/1.0"). Supply credentials for push or
    // private pull; omit for anonymous pull.
    static util::expected<client, std::string>
    create(std::string registry_url, std::string repository,
           std::optional<credentials> creds = std::nullopt);

    // does a blob exist? HEAD; 200 -> true, 404 -> false.
    util::expected<bool, std::string> blob_exists(const std::string& digest);

    // fetch a blob's bytes (follows the 307 redirect to backing storage).
    util::expected<std::string, std::string>
    get_blob(const std::string& digest);

    // fetch a manifest by tag or digest.
    util::expected<manifest_response, std::string>
    get_manifest(const std::string& reference);

    // list the repository's tags.
    util::expected<std::vector<std::string>, std::string> list_tags();

    // list artifacts that refer to `digest` (replaces `oras discover`).
    util::expected<std::vector<descriptor>, std::string>
    referrers(const std::string& digest);

    // upload a blob via the monolithic POST-then-PUT handshake. the file is
    // streamed from disk (not held in memory), so large squashfs layers are
    // fine. no-op if the blob already exists.
    util::expected<void, std::string>
    put_blob(const std::string& digest, const std::filesystem::path& file);

    // upload an in-memory blob (config blobs, small payloads).
    util::expected<void, std::string>
    put_blob_bytes(const std::string& digest, const std::string& data);

    // PUT a manifest under `reference` (tag or digest). returns the registry's
    // Docker-Content-Digest (the canonical id).
    util::expected<std::string, std::string>
    put_manifest(const std::string& reference, const std::string& body,
                 std::string_view media_type = media_type_manifest);

    const std::string& registry_url() const {
        return registry_url_;
    }
    const std::string& repository() const {
        return repository_;
    }

  private:
    client() = default;

    // return a bearer token for this repository, fetching/caching as needed.
    util::expected<std::string, std::string> token_for(bool write);

    std::string registry_url_; // normalised, no trailing '/'
    std::string repository_;
    std::optional<credentials> creds_;
    bearer_challenge challenge_;
    std::optional<std::string> pull_token_;
    std::optional<std::string> push_token_;
};

} // namespace oci
} // namespace uenv
