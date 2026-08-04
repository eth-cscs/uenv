#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>
#include <curl/easy.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <util/curl.h>
#include <util/defer.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/strings.h>

namespace util {

namespace curl {

std::optional<std::string> headers::get(std::string_view name) const {
    if (auto it = entries.find(to_lower(name)); it != entries.end()) {
        return it->second;
    }
    return std::nullopt;
}

void headers::set(std::string_view name, std::string value) {
    entries[to_lower(name)] = std::move(value);
}

void parse_header_line(headers& h, std::string_view line) {
    line = trim(line); // drops trailing CRLF and surrounding whitespace
    auto colon = line.find(':');
    // no colon -> status line ("HTTP/1.1 200 OK") or blank separator: ignore.
    if (colon == std::string_view::npos) {
        return;
    }
    auto name = trim(line.substr(0, colon));
    auto value = trim(line.substr(colon + 1));
    if (name.empty()) {
        return;
    }
    h.set(name, std::string{value});
}

// The credential sources, in the order they are consulted (see
// oci::resolve_credentials). Both the 401 and the 403 message need this: a 401
// usually means nothing was found, and a registry that hands out anonymous
// tokens reports the same situation as a 403 on the resource itself.
const char* credential_help =
    "uenv looks for credentials in the following order:\n"
    "  1. --token=PATH, where PATH is a token file, or a directory that "
    "holds a TOKEN file\n"
    "  2. the uenv token store, ~/.config/uenv/tokens/<registry-host>\n"
    "  3. the standard docker location, ~/.docker/config.json\n"
    "Sources 1 and 2 provide only the token: the username defaults to your "
    "login name,\nwhich is not available in a batch job, where it has to be "
    "passed with --username.";

std::string http_message(long code) {
    const char* default_message =
        "internal error contacting a network service - please create a CSCS "
        "service desk request with the output of running this command with the "
        "-vvv flag";
    const static std::unordered_map<long, std::string> messages = {
        {401, fmt::format("authentication is required - no credentials were "
                          "found, or the registry rejected them.\n\n{}",
                          credential_help)},
        {403,
         fmt::format(
             "the registry refused access to the requested resource.\nIf no "
             "credentials were provided, note that restricted uenv require "
             "them.\nIf they were, then the account is not permitted to "
             "perform the requested action, and access has to be granted."
             "\n\n{}",
             credential_help)},
        {408,
         "there was a time out contacting an external service - please retry "
         "later and create a CSCS Service Desk issue if the issue persists"},
    };

    if (messages.count(code)) {
        return messages.at(code);
    }
    // an unmapped 4xx is something about *this* request that the registry
    // refused - a service desk ticket is the wrong advice, so state the facts
    // and leave it there. only 5xx and transport-level failures fall through to
    // the "contact CSCS" message.
    if (code >= 400 && code < 500) {
        return fmt::format("the registry rejected the request (status {})",
                           code);
    }
    return default_message;
}

#define CURL_EASY(CMD)                                                         \
    if (auto rval__ = CMD; rval__ != CURLE_OK) {                               \
        return util::unexpected(error{rval__, errbuf});                        \
    }

size_t memory_callback(void* source, size_t size, size_t n, void* target) {
    const size_t realsize = size * n;
    spdlog::trace("curl::get memory callback {} bytes", realsize);
    std::vector<char>& result = *static_cast<std::vector<char>*>(target);
    char* src = static_cast<char*>(source);

    result.insert(result.end(), src, src + realsize);

    return realsize;
};

// Where curl should look for CA certificates. Either or both may be unset, in
// which case the corresponding curl option is left at its default.
struct ca_locations {
    std::optional<std::string>
        cainfo; // single PEM bundle file (CURLOPT_CAINFO)
    std::optional<std::string> capath; // c_rehash'd directory (CURLOPT_CAPATH)
};

// Locate the system trust store.
//
// curl is linked against a vendored static OpenSSL whose compiled-in OPENSSLDIR
// points at the build prefix, not the host, so OpenSSL's own default-verify
// paths do not find the real certificates. We therefore discover them here and
// set CURLOPT_CAINFO/CAPATH explicitly. Verification is never disabled: if
// nothing is found we leave curl at its default and let TLS fail closed with a
// clear "unable to get local issuer" error.
//
// Probe order (first hit wins for each of file / dir):
//   1. SSL_CERT_FILE / SSL_CERT_DIR from the startup environment snapshot
//      (operator escape hatch), when an env has been supplied via configure_tls
//   2. a well-known single-file bundle (preferred: portable, no rehash needed)
//   3. a well-known hashed directory
ca_locations resolve_ca_locations(const envvars::state* env) {
    namespace fs = std::filesystem;
    ca_locations ca;

    if (env) {
        if (auto f = env->get("SSL_CERT_FILE");
            f && !f->empty() && fs::exists(*f)) {
            ca.cainfo = *f;
        }
        if (auto d = env->get("SSL_CERT_DIR");
            d && !d->empty() && fs::is_directory(*d)) {
            ca.capath = *d;
        }
    }

    // candidate single-file bundles, most-specific host first
    const char* bundle_files[] = {
        "/var/lib/ca-certificates/ca-bundle.pem", // Alps / SUSE
        "/etc/ssl/certs/ca-certificates.crt",     // Debian / Ubuntu
        "/etc/pki/tls/certs/ca-bundle.crt",       // RHEL / Fedora
        "/etc/ssl/cert.pem",                      // misc
    };
    if (!ca.cainfo) {
        for (const char* p : bundle_files) {
            if (fs::exists(p)) {
                ca.cainfo = p;
                break;
            }
        }
    }

    // fall back to a hashed directory only if no bundle file was found
    const char* bundle_dirs[] = {
        "/etc/ssl/certs",
        "/etc/pki/tls/certs",
    };
    if (!ca.cainfo && !ca.capath) {
        for (const char* p : bundle_dirs) {
            if (fs::is_directory(p)) {
                ca.capath = p;
                break;
            }
        }
    }

    spdlog::debug("curl: CA bundle file={} dir={}",
                  ca.cainfo.value_or("<none>"), ca.capath.value_or("<none>"));
    return ca;
}

// CA locations resolved once from the startup environment snapshot by
// configure_tls(). Populated at process start; read by perform()/upload().
std::optional<ca_locations> configured_ca;

// The CA locations to apply. If configure_tls() has run we use its result
// (which honours SSL_CERT_FILE / SSL_CERT_DIR); otherwise we fall back to a
// filesystem-only probe with no environment overrides.
const ca_locations& active_ca_locations() {
    if (configured_ca) {
        return *configured_ca;
    }
    static const ca_locations fallback = resolve_ca_locations(nullptr);
    return fallback;
}

void configure_tls(const envvars::state& env) {
    configured_ca = resolve_ca_locations(&env);
}

expected<std::string, error> post(const std::string& data, std::string url,
                                  std::optional<std::string> content_type,
                                  long timeout_ms) {
    spdlog::trace("curl::post enter");

    request req;
    req.url = std::move(url);
    req.method = http_method::post;
    req.body = data;
    if (content_type) {
        req.header_lines.push_back(
            fmt::format("Content-Type: {}", *content_type));
    }
    // preserve legacy behaviour: caller-supplied total timeout, libcurl's
    // default connect timeout, and no redirect following.
    req.timeout_ms = timeout_ms;
    req.connect_timeout_ms = 300000L; // libcurl default
    req.follow_redirects = false;

    auto resp = perform(req);
    if (!resp) {
        return unexpected{resp.error()};
    }
    spdlog::trace("curl::post finished and retrieved data of size {}",
                  resp->body.size());
    return resp->body;
}

expected<std::string, error> get(std::string url) {
    request req;
    req.url = std::move(url);
    req.method = http_method::get;
    // wait up to 4s to connect and no more than 5s for the whole operation.
    req.connect_timeout_ms = 4000L;
    req.timeout_ms = 5000L;
    req.follow_redirects = false;

    auto resp = perform(req);
    if (!resp) {
        return unexpected{resp.error()};
    }
    spdlog::trace("curl::get finished and retrieved data of size {}",
                  resp->body.size());
    return resp->body;
}

expected<std::string, error> upload(std::string url,
                                    std::filesystem::path file_path) {
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    spdlog::trace("curl::upload enter");

    // File to upload
    FILE* file = fopen(file_path.c_str(), "rb");
    if (!file) {
        return unexpected{error{CURLE_FAILED_INIT, "Failed to open file"}};
    }

    // Initialize curl
    auto h = curl_easy_init();
    if (!h) {
        return unexpected{
            error{CURLE_FAILED_INIT, "Unable to initialize curl."}};
    }
    auto _ = defer([h]() { curl_easy_cleanup(h); });
    spdlog::trace("curl::upload easy init");

    // configure error message buffer
    CURL_EASY(curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf));

    // Configure curl options
    CURL_EASY(curl_easy_setopt(h, CURLOPT_URL, url.c_str()));
    spdlog::trace("curl::upload set url {}", url);
    CURL_EASY(curl_easy_setopt(h, CURLOPT_UPLOAD, 1L)); // Enable uploading
    CURL_EASY(curl_easy_setopt(h, CURLOPT_READDATA,
                               file)); // Set the file to read from
    spdlog::trace("curl::upload set file {}", file_path.c_str());

    // Set the size of the file (if known)
    curl_off_t file_size = std::filesystem::file_size(file_path);
    CURL_EASY(curl_easy_setopt(h, CURLOPT_INFILESIZE_LARGE, file_size));
    spdlog::trace(fmt::format("curl::upload file size: {}", file_size));

    // -X POST
    CURL_EASY(curl_easy_setopt(h, CURLOPT_POST, 1L)); // Use POST method

    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, memory_callback));
    spdlog::trace("curl::upload set memory callback");

    CURL_EASY(curl_easy_setopt(h, CURLOPT_USE_SSL, CURLUSESSL_ALL));

    // point TLS at the runtime-resolved system trust store (see perform())
    {
        const auto& ca = active_ca_locations();
        if (ca.cainfo) {
            CURL_EASY(curl_easy_setopt(h, CURLOPT_CAINFO, ca.cainfo->c_str()));
        }
        if (ca.capath) {
            CURL_EASY(curl_easy_setopt(h, CURLOPT_CAPATH, ca.capath->c_str()));
        }
    }

    CURL_EASY(curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 5000L));

    // some servers do not like requests that are made without a user-agent
    // field, so we provide one
    CURL_EASY(curl_easy_setopt(h, CURLOPT_USERAGENT, "libcurl-agent/1.0"));
    spdlog::trace("curl::upload set user agent");

    // we pass our 'chunk' struct to the callback function
    std::vector<char> result;
    result.reserve(200000);
    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEDATA, (void*)&result));
    spdlog::trace("curl::upload set memory target");

    CURL_EASY(curl_easy_setopt(h, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1));

    // Perform the request
    CURL_EASY(curl_easy_perform(h));

    // Get the HTTP response code
    long http_code = 0;
    CURL_EASY(curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_code));
    spdlog::trace("curl::upload http_code: {}", http_code);

    // Clean up
    fclose(file);

    // store stdout
    std::string curl_stdout{result.data(), result.data() + result.size()};

    if (http_code >= 400) {
        return unexpected{
            error{CURLE_HTTP_RETURNED_ERROR,
                  fmt::format("{}: {} \n {}", http_code,
                              http_message(http_code), curl_stdout)}};
    }

    return curl_stdout;
}

expected<void, error> del(std::string url, std::string username,
                          std::string token) {
    request req;
    req.url = url;
    req.method = http_method::del;
    req.username = username;
    req.password = token;
    req.connect_timeout_ms = 1000L;
    req.timeout_ms = 10000L;
    req.follow_redirects = false;

    auto resp = perform(req);
    if (!resp) {
        return unexpected{resp.error()};
    }
    spdlog::trace("curl::del http_code: {}", resp->status);

    if (resp->status >= 400) {
        return unexpected{error{
            CURLE_HTTP_RETURNED_ERROR,
            fmt::format("{}: {}", resp->status, http_message(resp->status))}};
    }

    spdlog::info("curl -X DELETE -u {}:{} {}", username,
                 std::string(token.size(), 'X'), url);

    spdlog::trace("curl::del successfully deleted {}", url);

    return {};
}

namespace {

size_t header_callback(char* buffer, size_t size, size_t nitems,
                       void* userdata) {
    const size_t total = size * nitems;
    auto& h = *static_cast<headers*>(userdata);
    parse_header_line(h, std::string_view{buffer, total});
    return total;
}

// download sink for the file path: the FILE* to write to, plus an optional tap
// (request::on_download_data) that observes each chunk written.
struct file_sink {
    FILE* file;
    const std::function<void(const char*, std::size_t)>* on_data;
};

size_t file_write_callback(void* source, size_t size, size_t n, void* target) {
    const size_t realsize = size * n;
    auto& sink = *static_cast<file_sink*>(target);
    const size_t written = fwrite(source, 1, realsize, sink.file);
    if (written > 0 && sink.on_data && *sink.on_data) {
        (*sink.on_data)(static_cast<const char*>(source), written);
    }
    return written;
}

int xferinfo_callback(void* p, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t ultotal, curl_off_t ulnow) {
    const auto& req = *static_cast<const request*>(p);
    if (req.on_download_progress) {
        req.on_download_progress(static_cast<std::uint64_t>(dlnow),
                                 static_cast<std::uint64_t>(dltotal));
    }
    if (req.on_upload_progress) {
        req.on_upload_progress(static_cast<std::uint64_t>(ulnow),
                               static_cast<std::uint64_t>(ultotal));
    }
    if (req.should_abort && req.should_abort()) {
        return 1; // non-zero aborts the transfer (CURLE_ABORTED_BY_CALLBACK)
    }
    return 0;
}

const char* method_name(http_method m) {
    switch (m) {
    case http_method::get:
        return "GET";
    case http_method::head:
        return "HEAD";
    case http_method::post:
        return "POST";
    case http_method::put:
        return "PUT";
    case http_method::patch:
        return "PATCH";
    case http_method::del:
        return "DELETE";
    }
    return "GET";
}

} // namespace

expected<response, error> perform(const request& req) {
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    spdlog::trace("curl::perform {} {}", method_name(req.method), req.url);

    CURL* h = curl_easy_init();
    if (!h) {
        return unexpected{
            error{CURLE_FAILED_INIT, "unable to initialise curl"}};
    }
    auto cleanup_handle = defer([h]() { curl_easy_cleanup(h); });

    CURL_EASY(curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_URL, req.url.c_str()));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_USERAGENT, "libcurl-agent/1.0"));
    // this client only ever speaks http(s), so say so: libcurl would otherwise
    // accept every protocol it was built with, and some of the urls it is
    // handed come from a registry response header (a Bearer realm, an upload
    // Location). Callers vet those too - this is the backstop, not the policy.
    CURL_EASY(curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https"));
    // HTTP/1.1 only. curl is built without nghttp2, so this is what would
    // happen anyway - state it so the transport does not change silently if
    // http2 is ever enabled in the build.
    CURL_EASY(curl_easy_setopt(h, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1));

    // method
    switch (req.method) {
    case http_method::get:
        CURL_EASY(curl_easy_setopt(h, CURLOPT_HTTPGET, 1L));
        break;
    case http_method::head:
        CURL_EASY(curl_easy_setopt(h, CURLOPT_NOBODY, 1L));
        break;
    default:
        CURL_EASY(curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST,
                                   method_name(req.method)));
        break;
    }

    // redirect following, opt-in only. when enabled, guard it: cap the redirect
    // count to avoid loops, and restrict redirects to https so a redirect can't
    // downgrade the transport. credentials are NOT carried across hosts -
    // CURLOPT_UNRESTRICTED_AUTH is deliberately left unset (libcurl default).
    if (req.follow_redirects) {
        CURL_EASY(curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L));
        CURL_EASY(curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L));
        CURL_EASY(curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "https"));
    }

    // assemble request headers
    struct curl_slist* slist = nullptr;
    auto cleanup_slist = defer([&slist]() {
        if (slist) {
            curl_slist_free_all(slist);
        }
    });
    for (const auto& line : req.header_lines) {
        slist = curl_slist_append(slist, line.c_str());
    }
    if (req.bearer_token) {
        slist = curl_slist_append(
            slist,
            fmt::format("Authorization: Bearer {}", *req.bearer_token).c_str());
    }
    if (slist) {
        CURL_EASY(curl_easy_setopt(h, CURLOPT_HTTPHEADER, slist));
    }

    // basic auth via libcurl (credentials never reach a command line / argv)
    if (req.username) {
        CURL_EASY(curl_easy_setopt(h, CURLOPT_USERNAME, req.username->c_str()));
    }
    if (req.password) {
        CURL_EASY(curl_easy_setopt(h, CURLOPT_PASSWORD, req.password->c_str()));
    }

    // request body. file upload takes precedence over an in-memory body.
    FILE* upload = nullptr;
    auto cleanup_upload = defer([&upload]() {
        if (upload) {
            fclose(upload);
        }
    });
    if (req.upload_file) {
        upload = fopen(req.upload_file->c_str(), "rb");
        if (!upload) {
            return unexpected{error{CURLE_READ_ERROR,
                                    fmt::format("unable to open {} for upload",
                                                req.upload_file->string())}};
        }
        curl_off_t file_size = std::filesystem::file_size(*req.upload_file);
        CURL_EASY(curl_easy_setopt(h, CURLOPT_UPLOAD, 1L));
        CURL_EASY(curl_easy_setopt(h, CURLOPT_READDATA, upload));
        CURL_EASY(curl_easy_setopt(h, CURLOPT_INFILESIZE_LARGE, file_size));
    } else if (req.body) {
        CURL_EASY(curl_easy_setopt(h, CURLOPT_POSTFIELDS, req.body->data()));
        CURL_EASY(
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)req.body->size()));
    }

    // capture the response body: either streamed to a file (for large blob
    // downloads) or buffered in memory.
    std::vector<char> body;
    FILE* download = nullptr;
    file_sink download_sink{};
    auto cleanup_download = defer([&download]() {
        if (download) {
            fclose(download);
        }
    });
    if (req.download_file) {
        download = fopen(req.download_file->c_str(), "wb");
        if (!download) {
            return unexpected{error{
                CURLE_WRITE_ERROR, fmt::format("unable to open {} for download",
                                               req.download_file->string())}};
        }
        download_sink.file = download;
        download_sink.on_data = &req.on_download_data;
        CURL_EASY(
            curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, file_write_callback));
        CURL_EASY(
            curl_easy_setopt(h, CURLOPT_WRITEDATA, (void*)&download_sink));
    } else {
        body.reserve(200000);
        CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, memory_callback));
        CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEDATA, (void*)&body));
    }

    // capture response headers
    response resp;
    CURL_EASY(curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, header_callback));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_HEADERDATA, (void*)&resp.headers));

    CURL_EASY(
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, req.connect_timeout_ms));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, req.timeout_ms));

    if (req.on_download_progress || req.on_upload_progress ||
        req.should_abort) {
        CURL_EASY(curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L));
        CURL_EASY(
            curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferinfo_callback));
        CURL_EASY(curl_easy_setopt(h, CURLOPT_XFERINFODATA, (void*)&req));
    }

    // point TLS at the runtime-resolved system trust store: curl links a
    // vendored static OpenSSL whose baked-in cert path does not exist on the
    // host, so the default-verify paths must be overridden here.
    {
        const auto& ca = active_ca_locations();
        if (ca.cainfo) {
            CURL_EASY(curl_easy_setopt(h, CURLOPT_CAINFO, ca.cainfo->c_str()));
        }
        if (ca.capath) {
            CURL_EASY(curl_easy_setopt(h, CURLOPT_CAPATH, ca.capath->c_str()));
        }
    }

    CURL_EASY(curl_easy_perform(h));

    CURL_EASY(curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &resp.status));
    resp.body.assign(body.data(), body.size());
    spdlog::trace("curl::perform got status {} ({} body bytes)", resp.status,
                  resp.body.size());

    return resp;
}

} // namespace curl
} // namespace util
