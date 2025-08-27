#include <string>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>
#include <curl/easy.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <util/curl.h>
#include <util/defer.h>
#include <util/expected.h>

namespace util {

namespace curl {

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

expected<std::string, error> post(const nlohmann::json& data, std::string url) {
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    auto str = data.dump();
    CURL* h = curl_easy_init();
    if (!h) {
        return unexpected{
            error{CURLE_FAILED_INIT, "unable to initialise curl"}};
    }
    auto _ = defer([h]() { curl_easy_cleanup(h); });

    // Set up curl options...
    CURL_EASY(curl_easy_setopt(h, CURLOPT_URL, url.c_str()));
    spdlog::trace("curl::post set url {}", url);
    // ... other setup ...
    CURL_EASY(curl_easy_setopt(h, CURLOPT_POSTFIELDS, str.c_str()));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, str.size()));
    spdlog::trace("curl::post set data {}", str);
    // Headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    CURL_EASY(curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers));

    // Set callback function to capture response
    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, memory_callback));
    // we pass our 'chunk' struct to the callback function
    std::vector<char> result;
    // when this was written, calls to the jfrog API returned roughly 100k
    // bytes
    result.reserve(200000);
    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEDATA, (void*)&result));
    spdlog::trace("curl::post set memory target");
    // Perform the request
    CURL_EASY(curl_easy_perform(h));

    spdlog::trace("curl::post finished and retrieved data of size {}",
                  result.size());

    return std::string{result.data(), result.data() + result.size()};
}

expected<std::string, error> get(std::string url) {
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;

    auto h = curl_easy_init();
    if (!h) {
        return unexpected{
            error{CURLE_FAILED_INIT, "unable to initialise curl"}};
    }
    auto _ = defer([h]() { curl_easy_cleanup(h); });

    CURL_EASY(curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf));

    CURL_EASY(curl_easy_setopt(h, CURLOPT_URL, url.c_str()));
    spdlog::trace("curl::get set url {}", url);

    // send all data to this function
    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, memory_callback));
    spdlog::trace("curl::get set memory callback");

    // we pass our 'chunk' struct to the callback function
    std::vector<char> result;
    // when this was written, calls to the jfrog API returned roughly 100k
    // bytes
    result.reserve(200000);
    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEDATA, (void*)&result));
    spdlog::trace("curl::get set memory target");

    // some servers do not like requests that are made without a user-agent
    // field, so we provide one
    CURL_EASY(curl_easy_setopt(h, CURLOPT_USERAGENT, "libcurl-agent/1.0"));
    spdlog::trace("curl::get set user agent");

    // set a reasonable time out - wait 1 second for connection, and no more
    // than 2 seconds for the whole operation.
    CURL_EASY(curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 4000L));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 5000L));
    spdlog::trace("curl::get set timeout");

    CURL_EASY(curl_easy_perform(h));

    spdlog::trace("curl::get finished and retrieved data of size {}",
                  result.size());

    return std::string{result.data(), result.data() + result.size()};
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

    CURL_EASY(curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf));
    spdlog::trace("curl::upload easy init");
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
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;

    auto h = curl_easy_init();
    if (!h) {
        return unexpected{
            error{CURLE_FAILED_INIT, "unable to initialise curl"}};
    }
    auto _ = defer([h]() { curl_easy_cleanup(h); });

    CURL_EASY(curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf));

    CURL_EASY(curl_easy_setopt(h, CURLOPT_URL, url.c_str()));
    spdlog::trace("curl::get set url {}", url);

    // send all data to this function
    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, memory_callback));
    spdlog::trace("curl::get set memory callback");

    // we pass our 'chunk' struct to the callback function
    std::vector<char> result;
    result.reserve(10000);
    CURL_EASY(curl_easy_setopt(h, CURLOPT_WRITEDATA, (void*)&result));
    spdlog::trace("curl::get set memory target");

    // some servers do not like requests that are made without a user-agent
    // field, so we provide one
    CURL_EASY(curl_easy_setopt(h, CURLOPT_USERAGENT, "libcurl-agent/1.0"));
    spdlog::trace("curl::get set user agent");

    // Specify the HTTP method as DELETE
    CURL_EASY(curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "DELETE"));

    // Set the username and password for basic authentication
    CURL_EASY(curl_easy_setopt(h, CURLOPT_USERNAME, username.c_str()));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_PASSWORD, token.c_str()));
    spdlog::trace("curl::get set credentials");

    CURL_EASY(curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 1000L));
    CURL_EASY(curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 10000L));
    spdlog::trace("curl::get set timeout");

    CURL_EASY(curl_easy_perform(h));

    // Get the HTTP response code
    long http_code = 0;
    CURL_EASY(curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_code));
    spdlog::trace("curl::upload http_code: {}", http_code);

    // store stdout
    std::string curl_stdout{result.data(), result.data() + result.size()};

    if (http_code >= 400) {
        return unexpected{
            error{CURLE_HTTP_RETURNED_ERROR,
                  fmt::format("{}: {}", http_code, http_message(http_code))}};
    }

    spdlog::info("curl -X DELETE -u {}:{} {}", username,
                 std::string(token.size(), 'X'), url);

    spdlog::trace("curl::get successfully deleted {}", url);

    return {};
}

} // namespace curl
} // namespace util
