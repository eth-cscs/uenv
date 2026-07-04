#include <unistd.h>

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <util/curl.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/strings.h>
#include <oci/auth.h>
#include <oci/parse.h>

namespace oci {

// --- pure helpers (no network; unit-tested) -----------------------------
// Kept out of the public auth.h interface; tests redeclare these prototypes
// inside namespace oci::impl (see test/unit/oci_auth.cpp). The bearer-challenge
// parser lives in src/oci/parse.cpp (oci::parse_bearer_challenge).
namespace impl {

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

} // namespace impl

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
            "could not parse WWW-Authenticate header '{}': {}", *header,
            challenge.error().message())};
    }
    return *challenge;
}

util::expected<std::string, std::string>
fetch_token(const bearer_challenge& challenge,
            const std::vector<std::string>& scopes,
            const std::optional<credentials>& creds) {
    util::curl::request req;
    req.url = impl::token_url(challenge, scopes);
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
    auto token = impl::parse_token_response(resp->body);
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

util::expected<std::optional<credentials>, std::string>
get_credentials(std::optional<std::string> username,
                std::optional<std::string> token) {
    namespace fs = std::filesystem;

    if (!token) {
        return std::nullopt;
    }

    fs::path token_path{token.value()};
    if (!fs::exists(token_path)) {
        return util::unexpected{fmt::format(
            "the token '{}' is not a path or file.", token_path.string())};
    }

    if (fs::is_directory(token_path)) {
        token_path = token_path / "TOKEN";
        if (!fs::exists(token_path)) {
            return util::unexpected{fmt::format(
                "the token file '{}' does not exist.", token_path.string())};
        }
    }

    if (util::file_access_level(token_path) < util::file_level::readonly) {
        return util::unexpected{fmt::format(
            "you do not have permission to read the token file '{}'",
            token_path.string())};
    }

    auto token_string = util::read_single_line_file(token_path);
    if (!token_string) {
        return util::unexpected{fmt::format("unable to read a token from '{}'",
                                            token_path.string())};
    }

    if (username) {
        return credentials{.username = username.value(),
                           .password = *token_string};
    }
    if (auto name = getlogin()) {
        return credentials{.username = name, .password = *token_string};
    }

    return util::unexpected{
        "provide a username with --username for the --token."};
}

} // namespace oci
