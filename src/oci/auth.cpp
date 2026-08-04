#include <cctype>
#include <csignal>
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
#include <util/subprocess.h>
#include <util/url.h>

namespace oci {

util::expected<std::optional<bearer_challenge>, std::string>
discover_challenge(const util::url& registry_url) {
    util::curl::request req;
    req.url = registry_url.origin().resolve("/v2/").string();
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

util::expected<token_response, std::string>
fetch_token(const util::url& registry, const bearer_challenge& challenge,
            const std::vector<std::string>& scopes,
            const std::optional<credentials>& creds) {
    // the realm came out of the registry's WWW-Authenticate header and is about
    // to receive the user's credentials: refuse it if it would put them on a
    // weaker transport than the registry itself.
    if (auto ok =
            detail::check_transport(registry, challenge.realm, "bearer realm");
        !ok) {
        return util::unexpected{ok.error()};
    }

    util::curl::request req;
    req.url = detail::token_url(challenge, scopes).string();
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

namespace {

// pair a token string with the caller-supplied username. Every token source
// funnels through here, so the "no username" error is worded once.
util::expected<credentials, std::string>
creds_from_token(std::string token,
                 const std::optional<std::string>& username) {
    if (!username) {
        return util::unexpected{std::string{username_required_error}};
    }
    return credentials{.username = *username, .password = std::move(token)};
}

} // namespace

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

    auto creds = creds_from_token(*token_string, username);
    if (!creds) {
        return util::unexpected{creds.error()};
    }
    return *creds;
}

namespace {

// A credential helper that exits without consuming its stdin leaves us writing
// the server URL into a pipe with no reader. Short writes still land in the
// pipe buffer, but one large enough to overrun it raises SIGPIPE, which is
// fatal by default.
class sigpipe_guard {
    void (*previous_)(int);

  public:
    sigpipe_guard() : previous_(std::signal(SIGPIPE, SIG_IGN)) {
    }
    ~sigpipe_guard() {
        std::signal(SIGPIPE, previous_);
    }
    sigpipe_guard(const sigpipe_guard&) = delete;
    sigpipe_guard& operator=(const sigpipe_guard&) = delete;
};

// the registry key to hand the helper on stdin. Helpers are keyed by the string
// the user logged in with, which may carry a scheme and path (docker hub stores
// "https://index.docker.io/v1/"), so prefer a configured key over the bare
// host.
std::string server_key(const nlohmann::json& cfg, const std::string& want) {
    for (const auto* section : {"credHelpers", "auths"}) {
        if (auto s = cfg.find(section); s != cfg.end() && s->is_object()) {
            for (const auto& entry : s->items()) {
                if (detail::normalise_host(entry.key()) == want) {
                    return entry.key();
                }
            }
        }
    }
    return want;
}

// query a docker credential helper: `docker-credential-<helper> get` takes the
// registry on stdin and answers with a JSON credential on stdout.
util::expected<std::optional<credentials>, std::string>
creds_from_helper(const std::string& helper, const std::string& server) {
    const auto program = fmt::format("docker-credential-{}", helper);
    // WARNING: the helper's stdout carries a secret. Never log it.
    auto proc = util::run({program, "get"});
    if (!proc) {
        return util::unexpected{
            fmt::format("unable to run the credential helper {}: {}", program,
                        proc.error())};
    }
    {
        sigpipe_guard guard;
        proc->in.putline(server);
        // the helper reads stdin to EOF before it replies.
        proc->in.close();
    }
    const auto out = proc->out.string();
    const auto err = proc->err.string();

    if (proc->wait() != 0) {
        // the helper's way of saying it holds nothing for this registry; fall
        // through to anonymous access rather than failing the command.
        if (err.find("credentials not found") != std::string::npos) {
            spdlog::debug("oci::creds_from_helper {} has no credential for {}",
                          program, server);
            return std::nullopt;
        }
        return util::unexpected{
            fmt::format("the credential helper {} failed (exit {}): {}",
                        program, proc->rvalue(), util::strip(err))};
    }
    spdlog::debug("oci::creds_from_helper {} answered for {}", program, server);
    return detail::parse_helper_output(out);
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
    const std::string want = detail::normalise_host(host);

    // docker consults a credential helper before the inline auths entry: a
    // `docker login` backed by a credential store leaves auths[host] empty.
    if (auto helper = detail::helper_for_host(j, want)) {
        return creds_from_helper(*helper, server_key(j, want));
    }

    auto auths = j.find("auths");
    if (auths == j.end() || !auths->is_object()) {
        return std::nullopt;
    }
    for (const auto& [key, entry] : auths->items()) {
        if (detail::normalise_host(key) != want || !entry.is_object()) {
            continue;
        }
        auto a = entry.find("auth");
        if (a == entry.end() || !a->is_string()) {
            spdlog::warn("docker config entry for {} has no 'auth' field and "
                         "no credential helper is configured for it",
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
        spdlog::debug("oci::resolve_credentials using the token {}",
                      src.explicit_token->string());
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
            spdlog::debug("oci::resolve_credentials using the token store "
                          "entry {}",
                          token_path.string());
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
        spdlog::debug("oci::resolve_credentials consulting the docker config "
                      "{} for {}",
                      src.docker_config->string(), registry_host);
        auto creds =
            creds_from_docker_config(*src.docker_config, registry_host);
        if (creds && !creds->has_value()) {
            spdlog::debug("oci::resolve_credentials no credentials for {}: "
                          "using anonymous access",
                          registry_host);
        }
        return creds;
    }

    // 4. no credentials found: anonymous access.
    spdlog::debug(
        "oci::resolve_credentials no credentials for {}: using anonymous "
        "access",
        registry_host);
    return std::nullopt;
}

} // namespace oci
