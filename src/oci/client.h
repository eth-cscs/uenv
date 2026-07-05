#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <oci/auth.h>
#include <oci/digest.h>
#include <oci/reference.h>
#include <util/expected.h>
#include <util/parse.h>
#include <util/sha256.h>

namespace oci {

// common OCI media types
inline constexpr std::string_view media_type_manifest =
    "application/vnd.oci.image.manifest.v1+json";
inline constexpr std::string_view media_type_index =
    "application/vnd.oci.image.index.v1+json";
inline constexpr std::string_view media_type_octet_stream =
    "application/octet-stream";

// An OCI content descriptor (a manifest entry / referrer record). A descriptor
// is not default-constructible: it must carry a valid `digest`, so a descriptor
// value is always well-formed.
struct descriptor {
    std::string media_type;
    oci::digest digest;
    std::size_t size = 0;
    std::optional<std::string> artifact_type;
    // inline base64 content (`data`), present on the empty config descriptor.
    std::optional<std::string> data;

    friend bool operator==(const descriptor&, const descriptor&) = default;
};

// A registry address split into an https base URL and a repository path prefix.
struct registry_location {
    std::string base;   // e.g. "https://jfrog.svc.cscs.ch"
    std::string prefix; // e.g. "uenv" (may be empty)
};

// Split a configured registry URL ("host", "host/prefix", or scheme-prefixed)
// into an https base URL and a repository prefix. The scheme, if any, is
// dropped and https is always used for the base (matching how pull resolves
// it).
util::expected<registry_location, util::parse_error>
split_registry(std::string_view configured_url);

// Build the OCI repository path for a uenv, e.g.
// "<prefix>/<nspace>/<system>/<uarch>/<name>/<version>" (the prefix segment is
// omitted when empty). This mirrors the address oras formed.
std::string repository_path(std::string_view prefix, std::string_view nspace,
                            std::string_view system, std::string_view uarch,
                            std::string_view name, std::string_view version);

// The result of fetching a manifest: the raw bytes plus the registry-reported
// digest and media type. The bytes are what must be re-digested locally to
// confirm identity.
struct manifest_response {
    std::string body;
    // value of the Docker-Content-Digest header, when present and well-formed.
    std::optional<oci::digest> digest;
    std::string media_type;
};

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
    util::expected<bool, std::string> blob_exists(const digest& d);

    // fetch a blob's bytes (follows the 307 redirect to backing storage).
    util::expected<std::string, std::string> get_blob(const digest& d);

    // stream a blob straight to a file, never holding it in memory (for the
    // multi-GB squashfs layer). follows the 307 redirect to backing storage.
    // an optional progress callback receives (bytes_downloaded, bytes_total);
    // an optional abort predicate, polled during transfer, cancels it.
    util::expected<void, std::string> get_blob_to_file(
        const digest& d, const std::filesystem::path& file,
        std::function<void(std::uint64_t, std::uint64_t)> progress = {},
        std::function<bool()> should_abort = {});

    // fetch a manifest by tag or digest.
    util::expected<manifest_response, std::string>
    get_manifest(const reference& ref);

    // list the repository's tags.
    util::expected<std::vector<std::string>, std::string> list_tags();

    // list artifacts that refer to `d` (replaces `oras discover`).
    util::expected<std::vector<descriptor>, std::string>
    referrers(const digest& d);

    // attempt a cross-repository blob mount from `from_repository` into this
    // client's repository (POST uploads/?mount=&from=). returns true if the
    // registry mounted the blob (201), false if it declined and wants a full
    // upload instead (202) — the caller should then fall back to copying. no-op
    // (returns true) if the blob already exists here.
    util::expected<bool, std::string>
    mount_blob(const digest& d, const std::string& from_repository);

    // upload a blob via the monolithic POST-then-PUT handshake. the file is
    // streamed from disk (not held in memory), so large squashfs layers are
    // fine. no-op if the blob already exists. an optional progress callback
    // receives (bytes_uploaded, bytes_total) during the PUT.
    util::expected<void, std::string>
    put_blob(const digest& d, const std::filesystem::path& file,
             std::function<void(std::uint64_t, std::uint64_t)> progress = {});

    // upload an in-memory blob (config blobs, small payloads).
    util::expected<void, std::string> put_blob_bytes(const digest& d,
                                                     const std::string& data);

    // PUT a manifest under `ref` (tag or digest).
    util::expected<void, std::string>
    put_manifest(const reference& ref, const std::string& body,
                 std::string_view media_type = media_type_manifest);

    // grant this client pull access to an additional repository, so its tokens
    // carry the scope a cross-repo blob mount needs (see mount_blob). must be
    // called before the first operation that fetches a token.
    void add_pull_scope(std::string repository);

    const std::string& registry_url() const {
        return registry_url_;
    }
    const std::string& repository() const {
        return repository_;
    }

  private:
    client() = default;

    // return a bearer token for this repository, fetching/caching as needed.
    // returns std::nullopt for an anonymous registry (no auth challenge), in
    // which case requests are sent without an Authorization header.
    util::expected<std::optional<std::string>, std::string>
    token_for(bool write);

    std::string registry_url_; // normalised, no trailing '/'
    std::string repository_;
    std::optional<credentials> creds_;
    // the auth challenge, or nullopt when the registry permits anonymous
    // access.
    std::optional<bearer_challenge> challenge_;
    std::optional<std::string> pull_token_;
    std::optional<std::string> push_token_;
    // extra repositories to request pull scope for (cross-repo mount).
    std::vector<std::string> extra_pull_scopes_;
};

} // namespace oci
