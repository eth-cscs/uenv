#pragma once

#include <string>

#include <util/expected.h>

#include <curl/curl.h>
#include <curl/easy.h>

namespace util {
namespace curl {

struct error {
    CURLcode code;
    std::string message;
};

std::string curl_get(std::string url);
expected<std::string, error> get(std::string url);

} // namespace curl
} // namespace util
