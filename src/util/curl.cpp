#include <memory>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <curl/easy.h>
#include <spdlog/spdlog.h>

#include <util/curl.h>
#include <util/expected.h>

namespace util {

namespace curl {

struct global {
    // curl_global_init(CURL_GLOBAL_DEFAULT);
    ~global() {
        curl_global_cleanup();
    }
};

struct handle {
    char errbuf[CURL_ERROR_SIZE];

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> state;
    ~handle() {
        spdlog::trace("curl::handle cleanup {}", (void*)state.get());
    }

    handle(CURL* ptr) : state(ptr, &curl_easy_cleanup) {
    }
    handle(const handle&) = delete;
    handle(handle&& other) = default;

    CURL* operator&() {
        return state.get();
    }

    CURL const* operator&() const {
        return state.get();
    }
};

expected<handle, error> make_handle() {
    CURL* ptr = curl_easy_init();
    if (!ptr) {
        spdlog::error("curl_easy_init returned null");
        return unexpected{error{CURLE_FAILED_INIT,
                                "unable to initialise curl easy interface"}};
    }
    spdlog::trace("curl:: created easy handle {}", (void*)ptr);
    handle h{ptr};
    if (h.state) {
        auto rc = curl_easy_setopt(&h, CURLOPT_ERRORBUFFER, h.errbuf);
        if (rc != CURLE_OK) {
            spdlog::error("curl: unable to set curl memory error buffer");
            return unexpected{error{rc, curl_easy_strerror(rc)}};
        }
    }
    return h;
}

#define CURL_EASY(CMD)                                                         \
    if (auto rval__ = CMD; rval__ != CURLE_OK) {                               \
        return util::unexpected(error{rval__, h.errbuf});                      \
    }

size_t memory_callback(void* source, size_t size, size_t n, void* target) {
    const size_t realsize = size * n;
    spdlog::trace("curl::get memory callback {} bytes", realsize);
    std::vector<char>& result = *static_cast<std::vector<char>*>(target);
    char* src = static_cast<char*>(source);

    result.insert(result.end(), src, src + realsize);

    return realsize;
};

expected<std::string, error> get(std::string url) {
    auto hd = curl::make_handle();
    if (!hd) {
        return unexpected{hd.error()};
    }

    auto& h = *hd;

    CURL_EASY(curl_easy_setopt(&h, CURLOPT_URL, url.c_str()));
    spdlog::trace("curl::get set url {}", url);

    // send all data to this function
    CURL_EASY(curl_easy_setopt(&h, CURLOPT_WRITEFUNCTION, memory_callback));
    spdlog::trace("curl::get set memory callback");

    // we pass our 'chunk' struct to the callback function
    std::vector<char> result;
    // when this was written, calls to the jfrog API returned roughly 100k bytes
    result.reserve(200000);
    CURL_EASY(curl_easy_setopt(&h, CURLOPT_WRITEDATA, (void*)&result));
    spdlog::trace("curl::get set memory target");

    // some servers do not like requests that are made without a user-agent
    // field, so we provide one
    CURL_EASY(curl_easy_setopt(&h, CURLOPT_USERAGENT, "libcurl-agent/1.0"));
    spdlog::trace("curl::get set user agent");

    // set a reasonable time out - wait 1 second for connection, and no more
    // than 2 seconds for the whole operation.
    CURL_EASY(curl_easy_setopt(&h, CURLOPT_CONNECTTIMEOUT_MS, 1000L));
    CURL_EASY(curl_easy_setopt(&h, CURLOPT_TIMEOUT_MS, 2000L));
    spdlog::trace("curl::get set timeout");

    CURL_EASY(curl_easy_perform(&h));
    spdlog::trace("curl::get finished and retrieved data of size {}",
                  result.size());

    return std::string{result.data(), result.data() + result.size()};
}

} // namespace curl
} // namespace util
