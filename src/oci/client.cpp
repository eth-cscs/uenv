#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <oci/client.h>
#include <oci/parse.h>
#include <oci/util.h>
#include <util/curl.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/sha256.h>

namespace oci {

// --- registry addressing helpers (pure) ---------------------------------

util::expected<registry_location, util::parse_error>
split_registry(std::string_view configured_url) {
    // parse the configured value as a URL; the scheme, if any, is dropped and
    // https is always used for the base. any port is preserved on the base.
    auto u = parse_url(configured_url);
    if (!u) {
        return util::unexpected(u.error());
    }

    // the repository prefix is the URL path with its surrounding slashes
    // trimmed.
    std::string prefix = u->path;
    if (!prefix.empty() && prefix.front() == '/') {
        prefix.erase(prefix.begin());
    }
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }

    // preserve an explicit scheme (e.g. http:// for a local test registry);
    // default to https when the config omits one (the CSCS deployment).
    const std::string scheme = u->scheme.empty() ? "https" : u->scheme;
    std::string base = scheme + "://" + u->host;
    if (u->port) {
        base += ':' + std::to_string(*u->port);
    }
    return registry_location{.base = std::move(base),
                             .prefix = std::move(prefix)};
}

std::string repository_path(std::string_view prefix, std::string_view nspace,
                            std::string_view system, std::string_view uarch,
                            std::string_view name, std::string_view version) {
    if (prefix.empty()) {
        return fmt::format("{}/{}/{}/{}/{}", nspace, system, uarch, name,
                           version);
    }
    return fmt::format("{}/{}/{}/{}/{}/{}", prefix, nspace, system, uarch, name,
                       version);
}

// --- client -------------------------------------------------------------

util::expected<client, std::string>
client::create(std::string registry_url, std::string repository,
               std::optional<credentials> creds) {
    auto challenge = discover_challenge(registry_url);
    if (!challenge) {
        return util::unexpected{challenge.error()};
    }

    client c;
    c.registry_url_ = std::move(registry_url);
    while (!c.registry_url_.empty() && c.registry_url_.back() == '/') {
        c.registry_url_.pop_back();
    }
    c.repository_ = std::move(repository);
    c.creds_ = std::move(creds);
    c.challenge_ = std::move(*challenge);
    return c;
}

util::expected<std::optional<std::string>, std::string>
client::token_for(bool write) {
    // anonymous registry: no challenge, so no token is needed or fetched.
    if (!challenge_) {
        return std::nullopt;
    }
    auto& cache = write ? push_token_ : pull_token_;
    if (cache) {
        return *cache;
    }
    std::vector<std::string> scopes;
    scopes.push_back(
        detail::repository_scope(repository_, write ? "pull,push" : "pull"));
    // cross-repo blob mounts need pull scope on the source repository too.
    for (const auto& r : extra_pull_scopes_) {
        scopes.push_back(detail::repository_scope(r, "pull"));
    }
    auto token = fetch_token(*challenge_, scopes, creds_);
    if (!token) {
        return util::unexpected{token.error()};
    }
    cache = *token;
    return *cache;
}

void client::add_pull_scope(std::string repository) {
    extra_pull_scopes_.push_back(std::move(repository));
}

util::expected<bool, std::string> client::blob_exists(const digest& d) {
    auto token = token_for(false);
    if (!token) {
        return util::unexpected{token.error()};
    }
    util::curl::request req;
    req.url = registry_url_ + detail::blob_path(repository_, d.string());
    req.method = util::curl::http_method::head;
    req.bearer_token = *token;

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    if (resp->status == 200) {
        return true;
    }
    if (resp->status == 404) {
        return false;
    }
    return util::unexpected{fmt::format("unexpected status {} for HEAD {}",
                                        resp->status, d.string())};
}

util::expected<std::string, std::string> client::get_blob(const digest& d) {
    auto token = token_for(false);
    if (!token) {
        return util::unexpected{token.error()};
    }
    util::curl::request req;
    req.url = registry_url_ + detail::blob_path(repository_, d.string());
    req.bearer_token = *token;
    // registries 307-redirect blob downloads to backing storage.
    req.follow_redirects = true;

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    if (resp->status != 200) {
        return util::unexpected{
            fmt::format("failed to fetch blob {} (status {}): {}", d.string(),
                        resp->status, util::curl::http_message(resp->status))};
    }
    return resp->body;
}

util::expected<void, std::string> client::get_blob_to_file(
    const digest& d, const std::filesystem::path& file,
    std::function<void(std::uint64_t, std::uint64_t)> progress,
    std::function<bool()> should_abort) {
    // check the destination directory is writable up front, so a bad target
    // surfaces as a clear error rather than a curl write failure.
    const auto parent = file.parent_path();
    if (!parent.empty() &&
        util::file_access_level(parent) != util::file_level::readwrite) {
        return util::unexpected{fmt::format(
            "cannot write blob to {}: {} is not a writable directory",
            file.string(), parent.string())};
    }

    auto token = token_for(false);
    if (!token) {
        return util::unexpected{token.error()};
    }

    // stream to a temporary name and rename into place only after the
    // transfer and digest verification succeed: a failed or interrupted
    // download must never leave a file at the final path, where a later
    // caller would mistake it for a complete blob.
    const std::filesystem::path partial{file.string() + ".partial"};

    util::curl::request req;
    req.url = registry_url_ + detail::blob_path(repository_, d.string());
    req.bearer_token = *token;
    req.follow_redirects = true;
    req.download_file = partial;
    req.on_download_progress = std::move(progress);
    req.should_abort = std::move(should_abort);

    // hash the blob as it streams to disk so its content can be verified
    // against the requested digest without a second read pass over a multi-GB
    // file.
    const bool verify = d.algorithm() == "sha256";
    util::sha256_state hash;
    if (verify) {
        util::sha256_init(hash);
        req.on_download_data = [&hash](const char* p, std::size_t n) {
            util::sha256_update(hash,
                                std::span<const std::byte>{
                                    reinterpret_cast<const std::byte*>(p), n});
        };
    }

    // every error path must remove the partial download before surfacing
    // the error.
    auto fail = [&partial](std::string msg) {
        spdlog::debug("oci::get_blob_to_file removing partial download {}",
                      partial.string());
        std::error_code ec;
        std::filesystem::remove(partial, ec);
        if (ec) {
            spdlog::warn("oci::get_blob_to_file unable to remove {}: {}",
                         partial.string(), ec.message());
        }
        return util::unexpected{std::move(msg)};
    };

    spdlog::debug("oci::get_blob_to_file downloading {} to {}", req.url,
                  partial.string());
    auto resp = util::curl::perform(req);
    if (!resp) {
        return fail(resp.error().message);
    }
    if (resp->status != 200) {
        return fail(fmt::format("failed to fetch blob {} (status {}): {}",
                                d.string(), resp->status,
                                util::curl::http_message(resp->status)));
    }

    if (verify) {
        const auto got = digest::from_sha256(util::sha256_final(hash));
        if (got != d) {
            return fail(
                fmt::format("downloaded blob digest mismatch: expected {}, "
                            "got {}",
                            d.string(), got.string()));
        }
        spdlog::debug("oci::get_blob_to_file verified {}", got.string());
    }

    spdlog::debug("oci::get_blob_to_file moving {} to {}", partial.string(),
                  file.string());
    std::error_code ec;
    std::filesystem::rename(partial, file, ec);
    if (ec) {
        return fail(fmt::format("unable to move downloaded blob {} to {}: {}",
                                partial.string(), file.string(), ec.message()));
    }
    return {};
}

util::expected<manifest_response, std::string>
client::get_manifest(const reference& ref) {
    auto token = token_for(false);
    if (!token) {
        return util::unexpected{token.error()};
    }
    util::curl::request req;
    req.url = registry_url_ + detail::manifest_path(repository_, ref.string());
    req.bearer_token = *token;
    req.header_lines = {fmt::format("Accept: {}", media_type_manifest),
                        fmt::format("Accept: {}", media_type_index)};

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    if (resp->status != 200) {
        return util::unexpected{fmt::format(
            "failed to fetch manifest {} (status {}): {}", ref.string(),
            resp->status, util::curl::http_message(resp->status))};
    }
    manifest_response m;
    m.body = std::move(resp->body);
    if (auto header = resp->headers.get("docker-content-digest")) {
        if (auto d = digest::parse(*header)) {
            m.digest = *d;
        }
    }
    m.media_type = resp->headers.get("content-type").value_or("");
    return m;
}

util::expected<std::vector<std::string>, std::string> client::list_tags() {
    auto token = token_for(false);
    if (!token) {
        return util::unexpected{token.error()};
    }
    util::curl::request req;
    req.url = registry_url_ + detail::tags_path(repository_);
    req.bearer_token = *token;

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    if (resp->status != 200) {
        return util::unexpected{
            fmt::format("failed to list tags (status {})", resp->status)};
    }
    auto tags = detail::parse_tags_list(resp->body);
    if (!tags) {
        return util::unexpected{"could not parse tags/list response"};
    }
    return *tags;
}

util::expected<std::vector<descriptor>, std::string>
client::referrers(const digest& d) {
    auto token = token_for(false);
    if (!token) {
        return util::unexpected{token.error()};
    }
    util::curl::request req;
    req.url = registry_url_ + detail::referrers_path(repository_, d.string());
    req.bearer_token = *token;

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    if (resp->status == 404) {
        // per the OCI 1.1 distribution spec, a 404 means the registry does
        // not implement the Referrers API (implementing registries return 200
        // with an empty index, even for a subject with no referrers). fall
        // back to the referrers tag schema that the push side maintains.
        spdlog::debug("oci::referrers {}: Referrers API unavailable (404), "
                      "falling back to the referrers tag schema",
                      d.string());
        return referrers_from_tag(d);
    }
    if (resp->status != 200) {
        return util::unexpected{fmt::format(
            "failed to fetch referrers of {} (status {}): {}", d.string(),
            resp->status, util::curl::http_message(resp->status))};
    }
    auto refs = detail::parse_referrers(resp->body);
    if (!refs) {
        return util::unexpected{"could not parse referrers response"};
    }
    return *refs;
}

util::expected<std::vector<descriptor>, std::string>
client::referrers_from_tag(const digest& d) {
    auto token = token_for(false);
    if (!token) {
        return util::unexpected{token.error()};
    }
    // the tag schema names an OCI image index <algo>-<hex> in the same
    // repository as the subject (see maintain_referrers_tag in push.cpp).
    const auto tag = d.algorithm() + "-" + d.hex();
    util::curl::request req;
    req.url = registry_url_ + detail::manifest_path(repository_, tag);
    req.bearer_token = *token;
    req.header_lines = {fmt::format("Accept: {}", media_type_index),
                        fmt::format("Accept: {}", media_type_manifest)};

    spdlog::debug("oci::referrers_from_tag fetching {}", req.url);
    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    if (resp->status == 404) {
        // the tag was never created: the subject has no referrers.
        spdlog::debug("oci::referrers_from_tag {}: tag absent, no referrers",
                      tag);
        return std::vector<descriptor>{};
    }
    if (resp->status != 200) {
        return util::unexpected{
            fmt::format("failed to fetch referrers tag {} (status {}): {}", tag,
                        resp->status, util::curl::http_message(resp->status))};
    }
    auto refs = detail::parse_referrers(resp->body);
    if (!refs) {
        return util::unexpected{
            fmt::format("could not parse referrers tag index {}", tag)};
    }
    spdlog::debug("oci::referrers_from_tag {}: {} referrer(s)", tag,
                  refs->size());
    return *refs;
}

namespace {

// drive the monolithic upload handshake: POST an upload session, then PUT the
// payload (carried by `body_setup`) to the returned Location with ?digest=.
util::expected<void, std::string>
do_put_blob(const std::string& registry_url, const std::string& uploads,
            const std::optional<std::string>& token, const std::string& digest,
            const std::function<void(util::curl::request&)>& body_setup) {
    // 1. open an upload session
    util::curl::request post;
    post.url = registry_url + uploads;
    post.method = util::curl::http_method::post;
    post.bearer_token = token;
    auto opened = util::curl::perform(post);
    if (!opened) {
        return util::unexpected{opened.error().message};
    }
    if (opened->status != 202) {
        return util::unexpected{fmt::format(
            "failed to open upload session (status {}): {} {}", opened->status,
            util::curl::http_message(opened->status), opened->body)};
    }
    auto location = opened->headers.get("location");
    if (!location) {
        return util::unexpected{
            "upload session response had no Location header"};
    }

    // 2. PUT the payload to <location>?digest=<digest>
    util::curl::request put;
    put.url = detail::resolve_upload_url(registry_url, *location, digest);
    put.method = util::curl::http_method::put;
    put.bearer_token = token;
    put.header_lines = {
        fmt::format("Content-Type: {}", media_type_octet_stream)};
    body_setup(put);

    auto done = util::curl::perform(put);
    if (!done) {
        return util::unexpected{done.error().message};
    }
    if (done->status != 201) {
        return util::unexpected{fmt::format(
            "failed to upload blob {} (status {}): {} {}", digest, done->status,
            util::curl::http_message(done->status), done->body)};
    }
    return {};
}

} // namespace

util::expected<bool, std::string>
client::mount_blob(const digest& d, const std::string& from_repository) {
    if (auto exists = blob_exists(d); exists && *exists) {
        return true;
    }
    auto token = token_for(true);
    if (!token) {
        return util::unexpected{token.error()};
    }
    util::curl::request post;
    post.url = registry_url_ + detail::uploads_path(repository_) +
               "?mount=" + d.string() + "&from=" + from_repository;
    post.method = util::curl::http_method::post;
    post.bearer_token = *token;

    auto resp = util::curl::perform(post);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    // 201: the blob was mounted. 202: the registry declined and opened an
    // upload session instead (caller must copy the blob the slow way).
    if (resp->status == 201) {
        return true;
    }
    if (resp->status == 202) {
        return false;
    }
    return util::unexpected{
        fmt::format("failed to mount blob {} from {} (status {}): {}",
                    d.string(), from_repository, resp->status, resp->body)};
}

util::expected<void, std::string>
client::put_blob(const digest& d, const std::filesystem::path& file,
                 std::function<void(std::uint64_t, std::uint64_t)> progress) {
    if (util::file_access_level(file) < util::file_level::readonly) {
        return util::unexpected{fmt::format(
            "cannot upload blob: {} is not a readable file", file.string())};
    }
    if (auto exists = blob_exists(d); exists && *exists) {
        spdlog::trace("oci::put_blob {} already present", d.string());
        return {};
    }
    auto token = token_for(true);
    if (!token) {
        return util::unexpected{token.error()};
    }
    return do_put_blob(registry_url_, detail::uploads_path(repository_), *token,
                       d.string(), [&file, &progress](util::curl::request& r) {
                           r.upload_file = file;
                           if (progress) {
                               r.on_upload_progress = progress;
                           }
                       });
}

util::expected<void, std::string>
client::put_blob_bytes(const digest& d, const std::string& data) {
    if (auto exists = blob_exists(d); exists && *exists) {
        return {};
    }
    auto token = token_for(true);
    if (!token) {
        return util::unexpected{token.error()};
    }
    return do_put_blob(registry_url_, detail::uploads_path(repository_), *token,
                       d.string(),
                       [&data](util::curl::request& r) { r.body = data; });
}

util::expected<void, std::string>
client::put_manifest(const reference& ref, const std::string& body,
                     std::string_view media_type) {
    auto token = token_for(true);
    if (!token) {
        return util::unexpected{token.error()};
    }
    util::curl::request req;
    req.url = registry_url_ + detail::manifest_path(repository_, ref.string());
    req.method = util::curl::http_method::put;
    req.bearer_token = *token;
    req.body = body;
    req.header_lines = {fmt::format("Content-Type: {}", media_type)};

    auto resp = util::curl::perform(req);
    if (!resp) {
        return util::unexpected{resp.error().message};
    }
    if (resp->status != 201) {
        return util::unexpected{fmt::format(
            "failed to put manifest {} (status {}): {} {}", ref.string(),
            resp->status, util::curl::http_message(resp->status), resp->body)};
    }
    return {};
}

} // namespace oci
