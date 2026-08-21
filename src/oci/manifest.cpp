#include <ctime>
#include <optional>
#include <string>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <oci/digest.h>
#include <oci/manifest.h>
#include <oci/types.h>
#include <oci/util.h>

namespace oci {

std::string rfc3339(std::tm tm) {
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string rfc3339_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    return rfc3339(tm);
}

manifest make_squashfs_manifest(digest layer_digest, std::size_t layer_size,
                                std::string created) {
    manifest m;
    m.artifact_type = std::string{artifact_type_squashfs};
    m.annotations[std::string{annotation_created}] = std::move(created);
    m.layers.push_back(manifest_layer{
        .media_type = std::string{media_type_layer_tar},
        .digest = layer_digest,
        .size = layer_size,
        .annotations = {{std::string{annotation_title}, "store.squashfs"}}});
    return m;
}

descriptor empty_config_descriptor() {
    return descriptor{
        .media_type = std::string{media_type_empty},
        .digest = digest::sha256(util::sha256::parse(empty_config_hex).value()),
        .size = empty_config_body.size(),
        .artifact_type = std::nullopt,
        .data = std::string{empty_config_data}};
}

std::optional<std::string> manifest_layer::title() const {
    if (auto it = annotations.find(std::string{annotation_title});
        it != annotations.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool manifest_layer::wants_unpack() const {
    auto it = annotations.find(std::string{annotation_unpack});
    return it != annotations.end() && it->second == "true";
}

const manifest_layer*
manifest::find_layer_by_title(std::string_view title) const {
    for (const auto& l : layers) {
        if (auto t = l.title(); t && *t == title) {
            return &l;
        }
    }
    return nullptr;
}

const manifest_layer* manifest::find_unpack_layer() const {
    for (const auto& l : layers) {
        if (l.wants_unpack()) {
            return &l;
        }
    }
    return nullptr;
}

//
// serialization
//

namespace {

nlohmann::ordered_json descriptor_json(const descriptor& d) {
    nlohmann::ordered_json j;
    j["mediaType"] = d.media_type;
    j["digest"] = d.digest.string();
    j["size"] = d.size;
    if (d.artifact_type) {
        j["artifactType"] = *d.artifact_type;
    }
    if (d.data) {
        j["data"] = *d.data;
    }
    return j;
}

nlohmann::ordered_json
annotations_json(const std::map<std::string, std::string>& annotations) {
    nlohmann::ordered_json j;
    // std::map iterates in sorted key order, matching oras.
    for (const auto& [k, v] : annotations) {
        j[k] = v;
    }
    return j;
}

} // namespace

std::string serialize_manifest(const manifest& m) {
    nlohmann::ordered_json j;
    j["schemaVersion"] = 2;
    j["mediaType"] = m.media_type;
    if (!m.artifact_type.empty()) {
        j["artifactType"] = m.artifact_type;
    }
    j["config"] = descriptor_json(m.config);

    auto layers = nlohmann::ordered_json::array();
    for (const auto& l : m.layers) {
        nlohmann::ordered_json lj;
        lj["mediaType"] = l.media_type;
        lj["digest"] = l.digest.string();
        lj["size"] = l.size;
        if (!l.annotations.empty()) {
            lj["annotations"] = annotations_json(l.annotations);
        }
        layers.push_back(std::move(lj));
    }
    j["layers"] = std::move(layers);

    if (m.subject) {
        j["subject"] = descriptor_json(*m.subject);
    }
    if (!m.annotations.empty()) {
        j["annotations"] = annotations_json(m.annotations);
    }

    return j.dump();
}

std::string serialize_index(const std::vector<descriptor>& manifests) {
    nlohmann::ordered_json j;
    j["schemaVersion"] = 2;
    j["mediaType"] = media_type_index;
    auto arr = nlohmann::ordered_json::array();
    for (const auto& d : manifests) {
        arr.push_back(descriptor_json(d));
    }
    j["manifests"] = std::move(arr);
    return j.dump();
}

//
// parsing
//

namespace {

util::expected<descriptor, std::string>
parse_descriptor(const nlohmann::json& j) {
    auto dg = digest::parse(detail::json_string_or(j, "digest", {}));
    if (!dg) {
        return util::unexpected{dg.error().message()};
    }
    descriptor d{.media_type = detail::json_string_or(j, "mediaType", {}),
                 .digest = *dg,
                 .size = detail::json_size_or(j, "size", 0)};
    if (auto a = j.find("artifactType"); a != j.end() && a->is_string()) {
        d.artifact_type = a->get<std::string>();
    }
    if (auto dt = j.find("data"); dt != j.end() && dt->is_string()) {
        d.data = dt->get<std::string>();
    }
    return d;
}

std::map<std::string, std::string> parse_annotations(const nlohmann::json& j) {
    std::map<std::string, std::string> out;
    if (auto a = j.find("annotations"); a != j.end() && a->is_object()) {
        for (const auto& [k, v] : a->items()) {
            if (v.is_string()) {
                out[k] = v.get<std::string>();
            }
        }
    }
    return out;
}

} // namespace

util::expected<manifest, std::string> parse_manifest(std::string_view body) {
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return util::unexpected{"could not parse manifest JSON"};
    }

    manifest m;
    m.media_type = detail::json_string_or(j, "mediaType",
                                          std::string{media_type_manifest});
    m.artifact_type = detail::json_string_or(j, "artifactType", {});

    // reject multi-arch image indexes / manifest lists: they carry a
    // `manifests` array of per-platform descriptors instead of `layers`, so
    // treating one as an image manifest would silently yield an empty layer
    // set. uenv only ever deals with single-platform artifacts.
    if (m.media_type == media_type_index ||
        m.media_type ==
            "application/vnd.docker.distribution.manifest.list.v2+json" ||
        j.contains("manifests")) {
        return util::unexpected{
            "multi-arch image indexes (manifest lists) are not supported"};
    }

    if (auto c = j.find("config"); c != j.end() && c->is_object()) {
        auto d = parse_descriptor(*c);
        if (!d) {
            return util::unexpected{
                fmt::format("invalid config descriptor: {}", d.error())};
        }
        m.config = *d;
    }

    if (auto ls = j.find("layers"); ls != j.end() && ls->is_array()) {
        for (const auto& l : *ls) {
            if (!l.is_object()) {
                return util::unexpected{
                    "manifest layer entry is not a JSON object"};
            }
            auto dg = digest::parse(detail::json_string_or(l, "digest", {}));
            if (!dg) {
                return util::unexpected{fmt::format("invalid layer digest: {}",
                                                    dg.error().message())};
            }
            manifest_layer layer{.media_type =
                                     detail::json_string_or(l, "mediaType", {}),
                                 .digest = *dg,
                                 .size = detail::json_size_or(l, "size", 0),
                                 .annotations = parse_annotations(l)};
            m.layers.push_back(std::move(layer));
        }
    }

    if (auto s = j.find("subject"); s != j.end() && s->is_object()) {
        auto d = parse_descriptor(*s);
        if (!d) {
            return util::unexpected{
                fmt::format("invalid subject descriptor: {}", d.error())};
        }
        m.subject = *d;
    }

    m.annotations = parse_annotations(j);
    return m;
}

} // namespace oci
