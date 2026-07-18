#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <util/envvars.h>
#include <util/expected.h>

#include <curl/curl.h>
#include <curl/easy.h>

namespace util {
namespace curl {

struct error {
    CURLcode code;
    std::string message;
};

// Configure the TLS trust store from the process's startup environment
// snapshot.
//
// Call once at startup, before any HTTPS request. curl links a vendored static
// OpenSSL whose compiled-in certificate path does not exist on the host, so the
// CA bundle must be located at runtime. The environment is passed in explicitly
// (rather than read via getenv) so the dependency on the SSL_CERT_FILE /
// SSL_CERT_DIR overrides is visible in the interface. If this is never called,
// perform()/upload() fall back to a filesystem-only probe with no env
// overrides.
void configure_tls(const envvars::state& env);

// --- low-level request/response primitive -------------------------------
//
// A single configurable HTTP request used to build the native OCI registry
// client (the token dance, blob HEAD/PUT, manifest PUT/GET, referrers/tags).
// Unlike the convenience helpers below, perform() does NOT treat HTTP >= 400 as
// an error: a 401 auth challenge or a 404 blob-miss are normal steps in the OCI
// flow, so the status and response headers are always handed back to the
// caller.

enum class http_method { get, head, post, put, patch, del };

// Case-insensitive view of HTTP response headers. Header names are stored
// lower-cased (HTTP field names are case-insensitive); the last value seen for
// a given name wins.
struct headers {
    std::unordered_map<std::string, std::string> entries;

    std::optional<std::string> get(std::string_view name) const;
    void set(std::string_view name, std::string value);
};

struct request {
    std::string url;
    http_method method = http_method::get;

    // extra request headers, each a raw "Name: value" line.
    std::vector<std::string> header_lines;
    // when set, an "Authorization: Bearer <token>" header is added.
    std::optional<std::string> bearer_token;
    // HTTP basic-auth credentials (sent via libcurl, never on a command line).
    std::optional<std::string> username;
    std::optional<std::string> password;

    // request body. at most one of these should be set (used by
    // put/post/patch):
    //  - body:        an in-memory payload (e.g. a manifest blob)
    //  - upload_file: stream a file as the body (e.g. a squashfs layer)
    std::optional<std::string> body;
    std::optional<std::filesystem::path> upload_file;

    // when set, the response body is streamed to this file instead of being
    // buffered in memory (essential for multi-GB blob downloads). response.body
    // is left empty in that case.
    std::optional<std::filesystem::path> download_file;

    // optional download-progress callback: (bytes_downloaded, bytes_total).
    // bytes_total is 0 until the server reports a content length.
    std::function<void(std::uint64_t, std::uint64_t)> on_download_progress;

    // optional upload-progress callback: (bytes_uploaded, bytes_total).
    // driven from the same xferinfo callback; used for the upload_file path.
    std::function<void(std::uint64_t, std::uint64_t)> on_upload_progress;

    // optional tap on the response body as it streams to download_file: called
    // with each chunk actually written, before returning to libcurl. lets a
    // caller hash the blob on the fly and so verify its digest without a second
    // read pass over a multi-GB file. only invoked on the download_file path.
    std::function<void(const char* data, std::size_t len)> on_download_data;

    // optional abort predicate, polled during transfer; returning true aborts
    // the request (used to make a long download responsive to Ctrl-C).
    std::function<bool()> should_abort;

    // follow 3xx redirects. off by default (matching libcurl), since following
    // is an SSRF/credential-leak/protocol-downgrade surface that only specific
    // operations need (e.g. a blob GET that a registry 307-redirects to cloud
    // storage). when enabled, perform() caps the redirect count and restricts
    // redirects to https. note the blob *upload* Location arrives on a 202 (not
    // a 3xx), so it is captured via the response headers regardless.
    bool follow_redirects = false;

    long connect_timeout_ms = 5000;
    // total operation timeout; 0 means no limit (needed for large uploads).
    long timeout_ms = 0;
};

struct response {
    long status = 0;
    std::string body;
    curl::headers headers;
};

expected<response, error> perform(const request& req);

// a friendly, user-facing explanation for an HTTP status code (e.g. a hint that
// 403 means invalid credentials). falls back to a generic message. intended to
// enrich the terse "status N" errors reported by the OCI client.
std::string http_message(long code);

// parse a single raw HTTP header line ("Name: value\r\n") into a headers map.
// status lines and blank separators are ignored. exposed for unit testing.
void parse_header_line(headers& h, std::string_view line);

expected<std::string, error>
post(const std::string& data, std::string url,
     std::optional<std::string> content_type = std::nullopt,
     long timeout_ms = 0);

expected<std::string, error> get(std::string url);

expected<std::string, error> upload(std::string url,
                                    std::filesystem::path file_name);

expected<void, error> del(std::string url, std::string username,
                          std::string password);

} // namespace curl
} // namespace util
