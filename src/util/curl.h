#pragma once

#include <string>

#include <util/expected.h>

#include <curl/curl.h>
#include <curl/easy.h>
#include <filesystem>

namespace util {
namespace curl {

struct error {
    CURLcode code;
    std::string message;
};

std::string curl_get(std::string url);
expected<std::string, error> get(std::string url);

expected<std::string, error> upload(std::string url,
                                    std::filesystem::path file_name);

} // namespace curl
} // namespace util
