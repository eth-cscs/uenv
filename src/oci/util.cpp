#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <oci/digest.h>
#include <oci/util.h>
#include <util/strings.h>
#include <util/url.h>

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

util::expected<util::url, std::string>
resolve_upload_url(const util::url& registry_url, std::string_view location,
                   std::string_view digest) {
    // the Location may be absolute (the registry pointing the upload at
    // backing storage) or registry-relative. "absolute" means it carries a
    // scheme - parse and ask, rather than matching a "http://" prefix by hand:
    // the prefix test was case-sensitive, so a "HTTPS://..." Location was taken
    // for a relative path and spliced onto the registry base.
    if (auto abs = util::parse_url(location);
        abs && abs->scheme() != util::url_scheme::none) {
        if (auto ok = check_transport(registry_url, *abs, "upload Location");
            !ok) {
            return util::unexpected{ok.error()};
        }
        // append the digest query parameter the monolithic PUT requires.
        return abs->query_param("digest", digest);
    }

    // a registry-relative Location, which routinely carries a query of its own
    // ("/v2/.../uploads/abc?_state=xyz"). Graft it onto the origin and parse
    // the result rather than appending it to the path: the '?' has to split
    // path from query, and appending would bury it inside the path.
    std::string joined = registry_url.origin().string();
    if (!location.empty() && location.front() != '/') {
        joined += '/';
    }
    joined += location;
    auto u = util::parse_url(joined);
    if (!u) {
        return util::unexpected{
            fmt::format("the registry returned an upload Location that is not "
                        "a valid url: '{}'",
                        location)};
    }
    return u->query_param("digest", digest);
}

std::string json_string_or(const nlohmann::json& j, const char* key,
                           std::string fallback) {
    if (auto it = j.find(key); it != j.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return fallback;
}

std::size_t json_size_or(const nlohmann::json& j, const char* key,
                         std::size_t fallback) {
    if (auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
        return it->get<std::size_t>();
    }
    return fallback;
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
        auto dg = digest::parse(json_string_or(m, "digest", {}));
        if (!dg) {
            continue;
        }
        descriptor d{.media_type = json_string_or(m, "mediaType", {}),
                     .digest = *dg,
                     .size = json_size_or(m, "size", 0)};
        if (auto a = m.find("artifactType"); a != m.end() && a->is_string()) {
            d.artifact_type = a->get<std::string>();
        }
        out.push_back(std::move(d));
    }
    return out;
}

util::url token_url(const bearer_challenge& challenge,
                    const std::vector<std::string>& scopes) {
    // query_param picks '?' or '&' itself and encodes both sides, so a realm
    // that already carries a query just continues, and a `service` or `scope`
    // holding '&' can no longer inject parameters of its own.
    util::url url = challenge.realm;
    if (!challenge.service.empty()) {
        url = url.query_param("service", challenge.service);
    }
    for (const auto& scope : scopes) {
        url = url.query_param("scope", scope);
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

std::string normalise_host(std::string_view key) {
    // docker keys credentials by whatever string the user logged in with, so a
    // key may be a bare host, a host:port, or a full url
    // ("https://index.docker.io/v1/"). Parsing settles all three, and drops the
    // userinfo that the old hand-rolled version left attached (so a
    // "https://user@host/v1/" key could never match "host").
    if (auto u = util::parse_url(key)) {
        return u->host_port();
    }
    return std::string{util::strip(key)};
}

std::optional<std::string> helper_for_host(const nlohmann::json& cfg,
                                           std::string_view host) {
    const auto want = normalise_host(host);
    if (auto helpers = cfg.find("credHelpers");
        helpers != cfg.end() && helpers->is_object()) {
        for (const auto& entry : helpers->items()) {
            if (normalise_host(entry.key()) == want &&
                entry.value().is_string()) {
                return entry.value().get<std::string>();
            }
        }
    }
    if (auto store = cfg.find("credsStore");
        store != cfg.end() && store->is_string()) {
        if (auto name = store->get<std::string>(); !name.empty()) {
            return name;
        }
    }
    return std::nullopt;
}

util::expected<std::optional<credentials>, std::string>
parse_helper_output(std::string_view body) {
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return util::unexpected{
            "the credential helper returned malformed JSON"};
    }
    auto username = json_string_or(j, "Username", "");
    auto secret = json_string_or(j, "Secret", "");

    // a helper with nothing stored for the registry answers with empty fields.
    if (username.empty() && secret.empty()) {
        return std::nullopt;
    }
    // "<token>" is the docker sentinel for an identity (refresh) token in
    // Secret. Redeeming one needs the OAuth2 refresh_token grant, which the
    // token handshake in this module does not implement.
    if (username == "<token>") {
        return util::unexpected{
            "the credential helper returned an identity token, which uenv "
            "cannot redeem; pass the token directly with --token"};
    }
    if (username.empty() || secret.empty()) {
        return util::unexpected{
            "the credential helper returned an incomplete credential"};
    }
    return credentials{.username = std::move(username),
                       .password = std::move(secret)};
}

std::string repository_scope(std::string_view repository,
                             std::string_view actions) {
    return fmt::format("repository:{}:{}", repository, actions);
}

util::expected<void, std::string> check_transport(const util::url& registry,
                                                  const util::url& target,
                                                  std::string_view what) {
    const auto scheme = target.scheme();
    if (scheme != util::url_scheme::http && scheme != util::url_scheme::https) {
        return util::unexpected{fmt::format(
            "refusing to use the {} '{}': only http and https are supported",
            what, target.string())};
    }
    if (registry.scheme() == util::url_scheme::https &&
        scheme != util::url_scheme::https) {
        return util::unexpected{
            fmt::format("refusing to use the {} '{}': it would downgrade the "
                        "connection to {} from the https registry '{}'",
                        what, target.string(), target.scheme_text(),
                        registry.origin().string())};
    }
    return {};
}

} // namespace detail
} // namespace oci
