#pragma once

#include <chrono>
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
#include <util/curl.h>
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

// The error type of client registry operations: a human-readable message,
// plus the HTTP status code of the failed request when a response was
// received. `http_status` is nullopt for failures that never produced an
// HTTP response: transport errors (DNS, connect, TLS, dropped connection),
// token-fetch failures, and local errors (unwritable destination, digest
// mismatch). Callers that need to distinguish "not found" (often an expected
// outcome) from a transient failure branch on `http_status`; everything else
// just prints `message`.
struct client_error {
    std::string message;
    std::optional<long> http_status = std::nullopt;
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
    static util::expected<client, client_error>
    create(std::string registry_url, std::string repository,
           std::optional<credentials> creds = std::nullopt);

    // does a blob exist? HEAD; 200 -> true, 404 -> false.
    util::expected<bool, client_error> blob_exists(const digest& d);

    // fetch a blob's bytes (follows the 307 redirect to backing storage).
    util::expected<std::string, client_error> get_blob(const digest& d);

    // stream a blob straight to a file, never holding it in memory (for the
    // multi-GB squashfs layer). follows the 307 redirect to backing storage.
    // an optional progress callback receives (bytes_downloaded, bytes_total);
    // an optional abort predicate, polled during transfer, cancels it.
    util::expected<void, client_error> get_blob_to_file(
        const digest& d, const std::filesystem::path& file,
        std::function<void(std::uint64_t, std::uint64_t)> progress = {},
        std::function<bool()> should_abort = {});

    // fetch a manifest by tag or digest.
    util::expected<manifest_response, client_error>
    get_manifest(const reference& ref);

    // list the repository's tags.
    util::expected<std::vector<std::string>, client_error> list_tags();

    // list artifacts that refer to `d` (replaces `oras discover`). registries
    // that do not implement the OCI 1.1 Referrers API (404) are handled by
    // falling back to the referrers tag schema (the <algo>-<hex> tag that the
    // push side maintains); an absent tag means "no referrers".
    util::expected<std::vector<descriptor>, client_error>
    referrers(const digest& d);

    // attempt a cross-repository blob mount from `from_repository` into this
    // client's repository (POST uploads/?mount=&from=). returns true if the
    // registry mounted the blob (201), false if it declined and wants a full
    // upload instead (202) — the caller should then fall back to copying. no-op
    // (returns true) if the blob already exists here.
    util::expected<bool, client_error>
    mount_blob(const digest& d, const std::string& from_repository);

    // upload a blob via the monolithic POST-then-PUT handshake. the file is
    // streamed from disk (not held in memory), so large squashfs layers are
    // fine. no-op if the blob already exists. an optional progress callback
    // receives (bytes_uploaded, bytes_total) during the PUT.
    util::expected<void, client_error>
    put_blob(const digest& d, const std::filesystem::path& file,
             std::function<void(std::uint64_t, std::uint64_t)> progress = {});

    // upload an in-memory blob (config blobs, small payloads).
    util::expected<void, client_error> put_blob_bytes(const digest& d,
                                                      const std::string& data);

    // PUT a manifest under `ref` (tag or digest).
    util::expected<void, client_error>
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

    // a cached bearer token and the deadline after which it is considered
    // stale (absent when the token endpoint did not advertise a lifetime).
    struct cached_token {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
    };

    // refresh a cached token this long before its advertised expiry, so a
    // token that is about to lapse is not used to start a new request.
    static constexpr std::chrono::seconds token_refresh_margin{30};

    // return a bearer token for this repository, fetching/caching as needed;
    // a cached token past (or within token_refresh_margin of) its advertised
    // expiry is refetched. returns std::nullopt for an anonymous registry (no
    // auth challenge), in which case requests are sent without an
    // Authorization header.
    util::expected<std::optional<std::string>, std::string>
    token_for(bool write);

    // set the bearer token on `req` and perform it. if the registry rejects
    // a previously cached token with 401 — typically one whose lifetime ran
    // out during a preceding long transfer, without the token endpoint having
    // advertised expires_in — a fresh token is fetched and the request
    // retried once; `reset` (when set) runs before the retry so the caller
    // can rewind per-attempt state (e.g. a streaming hash). a 401 on a
    // freshly fetched token is a real denial and is returned as-is.
    util::expected<util::curl::response, client_error>
    authed_perform(util::curl::request& req, bool write,
                   const std::function<void()>& reset = {});

    // read the referrers of `d` from the referrers tag schema index
    // (<algo>-<hex>): the fallback for registries without the Referrers API.
    util::expected<std::vector<descriptor>, client_error>
    referrers_from_tag(const digest& d);

    // upload a blob via the monolithic POST-then-PUT handshake; `body_setup`
    // attaches the payload (a file to stream, or an in-memory body) to the
    // PUT request.
    util::expected<void, client_error>
    put_blob_impl(const std::string& digest,
                  const std::function<void(util::curl::request&)>& body_setup);

    std::string registry_url_; // normalised, no trailing '/'
    std::string repository_;
    std::optional<credentials> creds_;
    // the auth challenge, or nullopt when the registry permits anonymous
    // access.
    std::optional<bearer_challenge> challenge_;
    std::optional<cached_token> pull_token_;
    std::optional<cached_token> push_token_;
    // extra repositories to request pull scope for (cross-repo mount).
    std::vector<std::string> extra_pull_scopes_;
};

} // namespace oci

#include <fmt/core.h>
template <> class fmt::formatter<oci::client_error> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    template <typename FmtContext>
    auto format(oci::client_error const& e, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", e.message);
    }
};
