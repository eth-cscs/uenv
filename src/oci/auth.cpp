#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <util/curl.h>
#include <util/expected.h>

#include "auth.h"

namespace oci {

namespace {

std::string to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string_view trim(std::string_view s) {
    auto is_space = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    };
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// split a scope value on spaces (the OCI spec permits a single scope parameter
// carrying a space-separated list).
void append_scopes(std::vector<std::string>& out, std::string_view value) {
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() &&
               std::isspace(static_cast<unsigned char>(value[i]))) {
            ++i;
        }
        std::size_t start = i;
        while (i < value.size() &&
               !std::isspace(static_cast<unsigned char>(value[i]))) {
            ++i;
        }
        if (i > start) {
            out.emplace_back(value.substr(start, i - start));
        }
    }
}

} // namespace

std::optional<bearer_challenge>
parse_bearer_challenge(std::string_view v) {
    std::string_view s = trim(v);

    // case-insensitive "Bearer" scheme, followed by whitespace
    constexpr std::string_view scheme = "bearer";
    if (s.size() < scheme.size()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != scheme[i]) {
            return std::nullopt;
        }
    }
    s.remove_prefix(scheme.size());
    if (!s.empty() && !std::isspace(static_cast<unsigned char>(s.front()))) {
        return std::nullopt; // e.g. "BearerX" is not the Bearer scheme
    }
    s = trim(s);

    bearer_challenge challenge;

    std::size_t i = 0;
    while (i < s.size()) {
        // key = up to '=' or ','
        std::size_t key_start = i;
        while (i < s.size() && s[i] != '=' && s[i] != ',') {
            ++i;
        }
        std::string key = to_lower(trim(s.substr(key_start, i - key_start)));

        std::string value;
        if (i < s.size() && s[i] == '=') {
            ++i; // consume '='
            if (i < s.size() && s[i] == '"') {
                ++i; // consume opening quote
                std::string buf;
                while (i < s.size() && s[i] != '"') {
                    if (s[i] == '\\' && i + 1 < s.size()) {
                        buf.push_back(s[i + 1]);
                        i += 2;
                    } else {
                        buf.push_back(s[i]);
                        ++i;
                    }
                }
                if (i < s.size() && s[i] == '"') {
                    ++i; // consume closing quote
                }
                value = std::move(buf);
            } else {
                std::size_t val_start = i;
                while (i < s.size() && s[i] != ',') {
                    ++i;
                }
                value = std::string(trim(s.substr(val_start, i - val_start)));
            }
        }

        if (key == "realm") {
            challenge.realm = std::move(value);
        } else if (key == "service") {
            challenge.service = std::move(value);
        } else if (key == "scope") {
            append_scopes(challenge.scopes, value);
        }

        // advance past the separating comma and any whitespace
        while (i < s.size() && s[i] != ',') {
            ++i;
        }
        if (i < s.size() && s[i] == ',') {
            ++i;
        }
        s = trim(s.substr(i));
        i = 0;
    }

    if (challenge.realm.empty()) {
        return std::nullopt; // realm is mandatory
    }
    return challenge;
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

std::optional<std::string> parse_token_response(std::string_view body) {
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    if (auto it = j.find("token"); it != j.end() && it->is_string()) {
        return it->get<std::string>();
    }
    if (auto it = j.find("access_token"); it != j.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return std::nullopt;
}

std::string repository_scope(std::string_view repository,
                             std::string_view actions) {
    return fmt::format("repository:{}:{}", repository, actions);
}

util::expected<bearer_challenge, std::string>
discover_challenge(const std::string& registry_url) {
    // normalise: drop any trailing '/' before appending the v2 base path.
    std::string base = registry_url;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    util::curl::request req;
    req.url = base + "/v2/";
    spdlog::trace("oci::discover_challenge probing {}", req.url);

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{fmt::format("failed to probe {}: {}", req.url,
                                            resp.error().message)};
    }
    if (resp->status == 200) {
        return util::unexpected{fmt::format(
            "{} permits anonymous access and issued no auth challenge",
            req.url)};
    }
    if (resp->status != 401) {
        return util::unexpected{fmt::format(
            "unexpected status {} probing {}", resp->status, req.url)};
    }
    auto header = resp->headers.get("www-authenticate");
    if (!header) {
        return util::unexpected{fmt::format(
            "{} returned 401 with no WWW-Authenticate header", req.url)};
    }
    auto challenge = parse_bearer_challenge(*header);
    if (!challenge) {
        return util::unexpected{fmt::format(
            "could not parse WWW-Authenticate header: {}", *header)};
    }
    return *challenge;
}

util::expected<std::string, std::string>
fetch_token(const bearer_challenge& challenge,
            const std::vector<std::string>& scopes,
            const std::optional<credentials>& creds) {
    util::curl::request req;
    req.url = token_url(challenge, scopes);
    if (creds) {
        req.username = creds->username;
        req.password = creds->password;
    }
    spdlog::trace("oci::fetch_token requesting {}", req.url);

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{fmt::format("token request failed: {}",
                                            resp.error().message)};
    }
    if (resp->status != 200) {
        return util::unexpected{
            fmt::format("token request to {} failed with status {}: {}",
                        req.url, resp->status, resp->body)};
    }
    auto token = parse_token_response(resp->body);
    if (!token) {
        return util::unexpected{
            "token endpoint response did not contain a token"};
    }
    return *token;
}

util::expected<std::string, std::string>
authenticate(const std::string& registry_url,
             const std::vector<std::string>& scopes,
             const std::optional<credentials>& creds) {
    auto challenge = discover_challenge(registry_url);
    if (!challenge) {
        return util::unexpected{challenge.error()};
    }
    return fetch_token(*challenge, scopes, creds);
}

} // namespace oci
