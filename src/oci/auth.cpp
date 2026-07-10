#include <unistd.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <oci/auth.h>
#include <oci/parse.h>
#include <oci/util.h>
#include <util/curl.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/strings.h>

namespace oci {

util::expected<std::optional<bearer_challenge>, std::string>
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
        // the registry permits anonymous access and issued no challenge; the
        // client will operate tokenless.
        spdlog::trace("{} permits anonymous access", req.url);
        return std::nullopt;
    }
    if (resp->status != 401) {
        return util::unexpected{fmt::format("unexpected status {} probing {}",
                                            resp->status, req.url)};
    }
    auto header = resp->headers.get("www-authenticate");
    if (!header) {
        return util::unexpected{fmt::format(
            "{} returned 401 with no WWW-Authenticate header", req.url)};
    }
    auto challenge = parse_bearer_challenge(*header);
    if (!challenge) {
        return util::unexpected{
            fmt::format("could not parse WWW-Authenticate header '{}': {}",
                        *header, challenge.error().message())};
    }
    return *challenge;
}

util::expected<std::string, std::string>
fetch_token(const bearer_challenge& challenge,
            const std::vector<std::string>& scopes,
            const std::optional<credentials>& creds) {
    util::curl::request req;
    req.url = detail::token_url(challenge, scopes);
    if (creds) {
        req.username = creds->username;
        req.password = creds->password;
    }
    spdlog::trace("oci::fetch_token requesting {}", req.url);

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{
            fmt::format("token request failed: {}", resp.error().message)};
    }
    if (resp->status != 200) {
        return util::unexpected{
            fmt::format("token request to {} failed with status {}: {}",
                        req.url, resp->status, resp->body)};
    }
    auto token = detail::parse_token_response(resp->body);
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
    // anonymous registry: no token required.
    if (!*challenge) {
        return std::string{};
    }
    return fetch_token(**challenge, scopes, creds);
}

util::expected<std::optional<credentials>, std::string>
get_credentials(std::optional<std::string> username,
                std::optional<std::string> token) {
    namespace fs = std::filesystem;

    if (!token) {
        return std::nullopt;
    }

    fs::path token_path{token.value()};
    std::error_code ec;
    if (!fs::exists(token_path, ec)) {
        return util::unexpected{fmt::format(
            "the token '{}' is not a path or file.", token_path.string())};
    }

    if (fs::is_directory(token_path, ec)) {
        token_path = token_path / "TOKEN";
        if (!fs::exists(token_path, ec)) {
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

namespace {

// build credentials from a token string, taking the username from `username`
// or, failing that, the OS login name.
util::expected<credentials, std::string>
creds_from_token(std::string token,
                 const std::optional<std::string>& username) {
    if (username) {
        return credentials{.username = *username, .password = std::move(token)};
    }
    if (auto name = getlogin()) {
        return credentials{.username = name, .password = std::move(token)};
    }
    return util::unexpected{
        "provide a username with --username for the token."};
}

// normalise a docker config.json `auths` key (or a registry host) down to a
// bare host[:port] for comparison: drop any scheme and any trailing path.
std::string normalise_host(std::string_view key) {
    auto s = key;
    if (auto p = s.find("://"); p != std::string_view::npos) {
        s.remove_prefix(p + 3);
    }
    if (auto p = s.find('/'); p != std::string_view::npos) {
        s = s.substr(0, p);
    }
    return std::string{s};
}

// look up credentials for `host` in a docker config.json file. Returns nullopt
// when the file has no matching, usable entry.
util::expected<std::optional<credentials>, std::string>
creds_from_docker_config(const std::filesystem::path& cfg,
                         std::string_view host) {
    std::ifstream in(cfg, std::ios::binary);
    if (!in) {
        return std::nullopt; // no readable config: not an error, just no creds
    }
    std::string body((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return util::unexpected{
            fmt::format("could not parse docker config {}", cfg.string())};
    }
    auto auths = j.find("auths");
    if (auths == j.end() || !auths->is_object()) {
        return std::nullopt;
    }
    const std::string want = normalise_host(host);
    for (const auto& [key, entry] : auths->items()) {
        if (normalise_host(key) != want || !entry.is_object()) {
            continue;
        }
        auto a = entry.find("auth");
        if (a == entry.end() || !a->is_string()) {
            // credsStore / credHelpers / identitytoken entries need an external
            // helper we do not implement.
            spdlog::warn("docker config entry for {} has no 'auth' field "
                         "(credential helpers are not supported)",
                         want);
            return std::nullopt;
        }
        auto decoded = util::base64_decode(a->get<std::string>());
        if (!decoded) {
            return util::unexpected{fmt::format(
                "invalid base64 in docker config auth for {}", want)};
        }
        auto colon = decoded->find(':');
        if (colon == std::string::npos) {
            return util::unexpected{fmt::format(
                "malformed docker config auth for {} (expected user:pass)",
                want)};
        }
        return credentials{.username = decoded->substr(0, colon),
                           .password = decoded->substr(colon + 1)};
    }
    return std::nullopt;
}

} // namespace

util::expected<std::optional<credentials>, std::string>
resolve_credentials(std::string_view registry_host,
                    const credential_sources& src) {
    // 1. an explicit --token wins.
    if (src.explicit_token) {
        return get_credentials(src.username, src.explicit_token->string());
    }

    // 2. the uenv token store: <uenv_token_dir>/<registry_host>.
    if (src.uenv_token_dir) {
        auto token_path = *src.uenv_token_dir / std::string{registry_host};
        if (util::file_access_level(token_path) >= util::file_level::readonly) {
            // warn (but do not fail) if the token file is readable by group or
            // others, since it holds a secret.
            std::error_code ec;
            auto perms = std::filesystem::status(token_path, ec).permissions();
            using std::filesystem::perms;
            if (!ec && (perms & (perms::group_read | perms::others_read)) !=
                           perms::none) {
                spdlog::warn("token file {} is readable by group/others; "
                             "consider `chmod 600`",
                             token_path.string());
            }
            auto token = util::read_single_line_file(token_path);
            if (!token) {
                return util::unexpected{fmt::format(
                    "unable to read a token from {}", token_path.string())};
            }
            auto creds = creds_from_token(*token, src.username);
            if (!creds) {
                return util::unexpected{creds.error()};
            }
            return *creds;
        }
    }

    // 3. docker config.json (interop fallback).
    if (src.docker_config) {
        return creds_from_docker_config(*src.docker_config, registry_host);
    }

    // 4. no credentials found: anonymous access.
    return std::nullopt;
}

} // namespace oci
