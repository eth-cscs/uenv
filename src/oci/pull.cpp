#include <filesystem>
#include <string>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <oci/client.h>
#include <oci/digest.h>
#include <oci/manifest.h>
#include <oci/pull.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/subprocess.h>

namespace oci {

namespace fs = std::filesystem;

namespace {

// title annotation oras gives the squashfs layer.
constexpr std::string_view squashfs_title = "store.squashfs";

// extract a gzipped tar into `dest`, using the system tar.
util::expected<void, std::string> extract_targz(const fs::path& archive,
                                                const fs::path& dest) {
    auto proc =
        util::run({"tar", "-xzf", archive.string(), "-C", dest.string()});
    if (!proc) {
        return util::unexpected{
            fmt::format("unable to run tar to unpack meta: {}", proc.error())};
    }
    if (auto rc = proc->wait(); rc != 0) {
        return util::unexpected{
            fmt::format("tar failed to unpack meta (exit {})", rc)};
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

    if (auto ok = util::ensure_directory(store); !ok) {
        return util::unexpected{ok.error()};
    }

    const fs::path dest = store / "store.squashfs";
    spdlog::debug("oci::pull_squashfs {} -> {}", want.string(), dest.string());
    // get_blob_to_file streams the layer to disk and verifies its digest on the
    // fly (hashing as it downloads).
    if (auto ok = c.get_blob_to_file(want, dest, std::move(progress),
                                     std::move(should_abort));
        !ok) {
        return util::unexpected{ok.error().message};
    }
    return {};
}

util::expected<bool, std::string>
pull_meta(client& c, const digest& manifest_digest, const fs::path& store) {
    auto refs = c.referrers(manifest_digest);
    if (!refs) {
        return util::unexpected{refs.error().message};
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
        return util::unexpected{meta_manifest.error().message};
    }
    auto parsed = parse_manifest(meta_manifest->body);
    if (!parsed) {
        return util::unexpected{parsed.error()};
    }

    // the meta layer is the gzipped tar marked for unpacking (else the sole
    // one).
    const manifest_layer* layer = parsed->find_unpack_layer();
    if (!layer && !parsed->layers.empty()) {
        layer = &parsed->layers[0];
    }
    if (!layer) {
        return util::unexpected{"meta manifest has no layers"};
    }

    spdlog::debug("oci::pull_meta layer {}", layer->digest.string());
    if (auto ok = util::ensure_directory(store); !ok) {
        return util::unexpected{ok.error()};
    }

    // stage the archive in a private temp dir (unique name, not a predictable
    // path inside `store`) before handing it to tar; make_temp_dir registers it
    // for removal on exit. get_blob_to_file verifies the digest as it streams:
    // the extracted meta (env.json, views) is later sourced into user
    // environments, so it must not be taken on trust.
    auto work = util::make_temp_dir();
    if (!work) {
        return util::unexpected{work.error()};
    }
    const auto archive = work.value() / "meta.tar.gz";
    if (auto ok = c.get_blob_to_file(layer->digest, archive); !ok) {
        return util::unexpected{ok.error().message};
    }
    if (auto ok = extract_targz(archive, store); !ok) {
        return util::unexpected{ok.error()};
    }
    return true;
}

} // namespace oci
