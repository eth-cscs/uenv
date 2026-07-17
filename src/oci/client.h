#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <oci/auth.h>
#include <oci/digest.h>
#include <oci/reference.h>
#include <oci/types.h>
#include <util/expected.h>

namespace oci {

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

//
// forward-declared pimpl (in the style of util/lex.h's lexer_impl); the HTTP
// machinery and the token cache never leak into this header.
//
class client_impl;

// A client bound to one repository on one registry. Authenticates lazily,
// caching a pull token and (separately) a pull,push token as operations need
// them. Read operations work anonymously when no credentials are supplied.
//
// Move-only: obtain one from create() and pass it around by reference.
class client {
  public:
    // Probe the registry, parse its auth challenge, and bind to `repository`
    // (e.g. "deploy/todi/gh200/app/1.0"). Supply credentials for push or
    // private pull; omit for anonymous pull.
    static util::expected<client, client_error>
    create(std::string registry_url, std::string repository,
           std::optional<credentials> creds = std::nullopt);

    ~client();
    client(client&&) noexcept;
    client& operator=(client&&) noexcept;

    // does a blob exist? HEAD; 200 -> true, 404 -> false.
    util::expected<bool, client_error> blob_exists(const digest& d);

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

  private:
    explicit client(std::unique_ptr<client_impl> impl);

    std::unique_ptr<client_impl> impl_;
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
