#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <oci/client.h>
#include <oci/digest.h>
#include <oci/manifest.h>
#include <oci/pull.h>
#include <util/expected.h>
#include <util/subprocess.h>

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

    auto proc = util::run({"tar", "-xzf", tmp.string(), "-C", dest.string()});
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

util::expected<void, std::string> ensure_dir(const fs::path& store) {
    if (fs::exists(store)) {
        return {};
    }
    std::error_code ec;
    fs::create_directories(store, ec);
    if (ec) {
        return util::unexpected{
            fmt::format("unable to create {}: {}", store.string(), ec.message())};
    }
    return {};
}

} // namespace

util::expected<void, std::string>
pull_squashfs(client& c, const manifest& image, const fs::path& store,
              progress_fn progress, std::function<bool()> should_abort) {
    // pick the squashfs layer: prefer the one titled "store.squashfs", else the
    // sole layer.
    const manifest_layer* layer = image.find_layer_by_title(squashfs_title);
    if (!layer && image.layers.size() == 1) {
        layer = &image.layers[0];
    }
    if (!layer) {
        return util::unexpected{
            "could not find the store.squashfs layer in the manifest"};
    }
    const digest& want = layer->digest;

    if (auto ok = ensure_dir(store); !ok) {
        return util::unexpected{ok.error()};
    }

    const fs::path dest = store / "store.squashfs";
    spdlog::debug("oci::pull_squashfs {} -> {}", want.string(), dest.string());
    // get_blob_to_file streams the layer to disk and verifies its digest on the
    // fly (hashing as it downloads).
    if (auto ok = c.get_blob_to_file(want, dest, std::move(progress),
                                     std::move(should_abort));
        !ok) {
        return util::unexpected{ok.error()};
    }
    return {};
}

util::expected<bool, std::string>
pull_meta(client& c, const digest& manifest_digest, const fs::path& store) {
    auto refs = c.referrers(manifest_digest);
    if (!refs) {
        return util::unexpected{refs.error()};
    }

    // find the uenv/meta referrer.
    const descriptor* meta = nullptr;
    for (const auto& r : *refs) {
        if (r.artifact_type == artifact_type_meta) {
            meta = &r;
            break;
        }
    }
    if (!meta) {
        return false; // no attached meta
    }

    auto meta_manifest = c.get_manifest(reference::digest(meta->digest));
    if (!meta_manifest) {
        return util::unexpected{meta_manifest.error()};
    }
    auto parsed = parse_manifest(meta_manifest->body);
    if (!parsed) {
        return util::unexpected{parsed.error()};
    }

    // the meta layer is the gzipped tar marked for unpacking (else the sole one).
    const manifest_layer* layer = parsed->find_unpack_layer();
    if (!layer && !parsed->layers.empty()) {
        layer = &parsed->layers[0];
    }
    if (!layer) {
        return util::unexpected{"meta manifest has no layers"};
    }

    spdlog::debug("oci::pull_meta layer {}", layer->digest.string());
    auto blob = c.get_blob(layer->digest);
    if (!blob) {
        return util::unexpected{blob.error()};
    }

    if (auto ok = ensure_dir(store); !ok) {
        return util::unexpected{ok.error()};
    }
    if (auto ok = extract_targz(*blob, store); !ok) {
        return util::unexpected{ok.error()};
    }
    return true;
}

} // namespace oci
