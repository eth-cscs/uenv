#pragma once

#include <filesystem>
#include <optional>
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
