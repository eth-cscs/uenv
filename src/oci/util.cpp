#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <oci/digest.h>
#include <oci/util.h>

namespace oci {
namespace detail {

std::string blob_path(std::string_view repository, std::string_view digest) {
    return fmt::format("/v2/{}/blobs/{}", repository, digest);
}

std::string manifest_path(std::string_view repository,
                          std::string_view reference) {
    return fmt::format("/v2/{}/manifests/{}", repository, reference);
}

std::string uploads_path(std::string_view repository) {
    return fmt::format("/v2/{}/blobs/uploads/", repository);
}

std::string tags_path(std::string_view repository) {
    return fmt::format("/v2/{}/tags/list", repository);
}

std::string referrers_path(std::string_view repository,
                           std::string_view digest) {
    return fmt::format("/v2/{}/referrers/{}", repository, digest);
}

std::string resolve_upload_url(std::string_view registry_url,
                               std::string_view location,
                               std::string_view digest) {
    std::string url;
    // the Location may be absolute (https://...) or registry-relative
    // (/v2/...).
    if (location.rfind("http://", 0) == 0 ||
        location.rfind("https://", 0) == 0) {
        url = std::string{location};
    } else {
        std::string base{registry_url};
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        if (!location.empty() && location.front() != '/') {
            base.push_back('/');
        }
        url = base + std::string{location};
    }
    // append the digest query parameter the monolithic PUT requires.
    url += (url.find('?') == std::string::npos) ? '?' : '&';
    url += "digest=";
    url += digest;
    return url;
}

std::optional<std::vector<std::string>> parse_tags_list(std::string_view body) {
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    std::vector<std::string> tags;
    if (auto it = j.find("tags"); it != j.end() && it->is_array()) {
        for (const auto& t : *it) {
            if (t.is_string()) {
                tags.push_back(t.get<std::string>());
            }
        }
    }
    return tags;
}

std::optional<std::vector<descriptor>> parse_referrers(std::string_view body) {
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    std::vector<descriptor> out;
    auto it = j.find("manifests");
    if (it == j.end() || !it->is_array()) {
        return out;
    }
    for (const auto& m : *it) {
        if (!m.is_object()) {
            continue;
        }
        // a descriptor must carry a valid digest; skip malformed entries.
        auto dg = digest::parse(m.value("digest", std::string{}));
        if (!dg) {
            continue;
        }
        descriptor d{.media_type = m.value("mediaType", std::string{}),
                     .digest = *dg,
                     .size = m.value("size", std::size_t{0})};
        if (auto a = m.find("artifactType"); a != m.end() && a->is_string()) {
            d.artifact_type = a->get<std::string>();
        }
        out.push_back(std::move(d));
    }
    return out;
}

std::string token_url(const bearer_challenge& challenge,
                      const std::vector<std::string>& scopes) {
    std::string url = challenge.realm;
    char sep = url.find('?') == std::string::npos ? '?' : '&';
    if (!challenge.service.empty()) {
        url += sep;
        url += "service=";
        url += challenge.service;
        sep = '&';
    }
    for (const auto& scope : scopes) {
        url += sep;
        url += "scope=";
        url += scope;
        sep = '&';
    }
    return url;
}

std::optional<token_response> parse_token_response(std::string_view body) {
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    token_response out;
    if (auto it = j.find("token"); it != j.end() && it->is_string()) {
        out.token = it->get<std::string>();
    } else if (auto alt = j.find("access_token");
               alt != j.end() && alt->is_string()) {
        out.token = alt->get<std::string>();
    } else {
        return std::nullopt;
    }
    if (auto it = j.find("expires_in");
        it != j.end() && it->is_number_integer()) {
        out.expires_in = it->get<long>();
    }
    return out;
}

std::string repository_scope(std::string_view repository,
                             std::string_view actions) {
    return fmt::format("repository:{}:{}", repository, actions);
}

} // namespace detail
} // namespace oci
