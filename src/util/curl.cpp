#include <cctype>
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

std::string http_message(long code) {
    const char* default_message =
        "internal error contacting a network service - please create a CSCS "
        "service desk request with the output of running this command with the "
        "-vvv flag";
    const static std::unordered_map<long, std::string> messages = {
        {403, "the provided credentials were invalid - you might not have "
              "permission to access the requested resource."},
        {408,
         "there was a time out contacting an external service - please retry "
         "later and create a CSCS Service Desk issue if the issue persists"},
    };

    return messages.count(code) ? messages.at(code) : default_message;
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

    CURL_EASY(curl_easy_perform(h));

    CURL_EASY(curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &resp.status));
    resp.body.assign(body.data(), body.size());
    spdlog::trace("curl::perform got status {} ({} body bytes)", resp.status,
                  resp.body.size());

    return resp;
}

} // namespace curl
} // namespace util
