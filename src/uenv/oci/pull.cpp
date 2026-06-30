#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/sha256.h>
#include <util/subprocess.h>

#include "client.h"
#include "pull.h"

namespace uenv {
namespace oci {

namespace fs = std::filesystem;

namespace {

// title annotation oras gives the squashfs layer.
constexpr std::string_view squashfs_title = "store.squashfs";

// extract a gzipped tar (held in memory) into `dest`, using the system tar.
util::expected<void, std::string> extract_targz(const std::string& data,
                                                const fs::path& dest) {
    auto tmp = dest / ".uenv-meta.tar.gz";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return util::unexpected{
                fmt::format("unable to write temporary {}", tmp.string())};
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) {
            return util::unexpected{
                fmt::format("error writing temporary {}", tmp.string())};
        }
    }

    auto proc = util::run(
        {"tar", "-xzf", tmp.string(), "-C", dest.string()});
    if (!proc) {
        fs::remove(tmp);
        return util::unexpected{
            fmt::format("unable to run tar to unpack meta: {}", proc.error())};
    }
    auto rc = proc->wait();
    fs::remove(tmp);
    if (rc != 0) {
        return util::unexpected{
            fmt::format("tar failed to unpack meta (exit {})", rc)};
    }
    return {};
}

} // namespace

util::expected<void, std::string>
pull_squashfs(client& c, const manifest_response& manifest,
              const fs::path& store, progress_fn progress,
              std::function<bool()> should_abort) {
    auto j = nlohmann::json::parse(manifest.body, nullptr,
                                   /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.contains("layers")) {
        return util::unexpected{"image manifest has no layers"};
    }

    // pick the squashfs layer: prefer the one titled "store.squashfs", else the
    // sole layer.
    std::string digest;
    for (const auto& layer : j["layers"]) {
        auto ann = layer.find("annotations");
        if (ann != layer.end()) {
            auto title = ann->find("org.opencontainers.image.title");
            if (title != ann->end() && title->is_string() &&
                title->get<std::string>() == squashfs_title) {
                digest = layer.value("digest", "");
                break;
            }
        }
    }
    if (digest.empty() && j["layers"].size() == 1) {
        digest = j["layers"][0].value("digest", "");
    }
    if (digest.empty()) {
        return util::unexpected{
            "could not find the store.squashfs layer in the manifest"};
    }

    if (!fs::exists(store)) {
        std::error_code ec;
        fs::create_directories(store, ec);
        if (ec) {
            return util::unexpected{fmt::format("unable to create {}: {}",
                                                store.string(), ec.message())};
        }
    }

    const fs::path dest = store / "store.squashfs";
    spdlog::debug("oci::pull_squashfs {} -> {}", digest, dest.string());
    if (auto ok = c.get_blob_to_file(digest, dest, std::move(progress),
                                     std::move(should_abort));
        !ok) {
        return util::unexpected{ok.error()};
    }

    // self-verify: the on-disk file must re-digest to the layer digest.
    auto actual = util::sha256_file(dest);
    if (!actual) {
        return util::unexpected{actual.error()};
    }
    auto actual_digest = digest_string(*actual);
    if (actual_digest != digest) {
        std::error_code ec;
        fs::remove(dest, ec);
        return util::unexpected{fmt::format(
            "downloaded squashfs digest mismatch: expected {}, got {}", digest,
            actual_digest)};
    }
    spdlog::debug("oci::pull_squashfs verified {}", actual_digest);
    return {};
}

util::expected<bool, std::string>
pull_meta(client& c, const std::string& manifest_digest, const fs::path& store) {
    auto refs = c.referrers(manifest_digest);
    if (!refs) {
        return util::unexpected{refs.error()};
    }

    // find the uenv/meta referrer.
    std::string meta_manifest_digest;
    for (const auto& r : *refs) {
        if (r.artifact_type == "uenv/meta") {
            meta_manifest_digest = r.digest;
            break;
        }
    }
    if (meta_manifest_digest.empty()) {
        return false; // no attached meta
    }

    auto meta_manifest = c.get_manifest(meta_manifest_digest);
    if (!meta_manifest) {
        return util::unexpected{meta_manifest.error()};
    }
    auto j = nlohmann::json::parse(meta_manifest->body, nullptr,
                                   /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.contains("layers") || j["layers"].empty()) {
        return util::unexpected{"meta manifest has no layers"};
    }

    // the meta layer is the gzipped tar marked for unpacking.
    std::string digest;
    for (const auto& layer : j["layers"]) {
        auto ann = layer.find("annotations");
        if (ann != layer.end()) {
            auto unpack = ann->find("io.deis.oras.content.unpack");
            if (unpack != ann->end() && unpack->is_string() &&
                unpack->get<std::string>() == "true") {
                digest = layer.value("digest", "");
                break;
            }
        }
    }
    if (digest.empty()) {
        digest = j["layers"][0].value("digest", "");
    }

    spdlog::debug("oci::pull_meta layer {}", digest);
    auto blob = c.get_blob(digest);
    if (!blob) {
        return util::unexpected{blob.error()};
    }

    if (!fs::exists(store)) {
        std::error_code ec;
        fs::create_directories(store, ec);
        if (ec) {
            return util::unexpected{fmt::format("unable to create {}: {}",
                                                store.string(), ec.message())};
        }
    }

    if (auto ok = extract_targz(*blob, store); !ok) {
        return util::unexpected{ok.error()};
    }
    return true;
}

} // namespace oci
} // namespace uenv
