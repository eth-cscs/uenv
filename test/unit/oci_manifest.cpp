#include <catch2/catch_all.hpp>

#include <oci/digest.h>
#include <oci/manifest.h>
#include <oci/types.h>
#include <util/sha.h>
#include <util/strings.h>

using namespace oci;

namespace {
std::string digest_of(const std::string& s) {
    return digest::sha256(util::sha256_string(s)).string();
}
// build a sha256 digest from a known-valid hex string.
digest sha_digest(std::string_view hex) {
    return digest::sha256(util::sha256::parse(hex).value());
}
const std::string image_layer_hex =
    "b563b338a3797eb908b1f60a3b710b7021599aaac61d02f11b273bd2ff0986d4";
const std::string image_manifest_hex =
    "b50ca0d101456970ea94dccb5a33adcfd2b9a566bc36c716f4b64a0522089f48";
} // namespace

// empty_config_body, empty_config_hex and empty_config_data are three
// hand-written representations of the same two bytes, and
// empty_config_descriptor() simply copies all three: nothing re-derives one
// from another, and there is no base64 encoder in the tree that could. Without
// this case a typo in the sha or the base64 would compile, pass the suite, and
// only surface as a registry rejecting a pushed manifest.
//
// The base64 check is as strong as util::base64_decode, which discards the
// trailing bits that padding covers: a typo confined to those bits ("e30=" ->
// "e31=") decodes to "{}" all the same and is not caught here.
TEST_CASE("empty config constants agree", "[oci][manifest]") {
    // empty_config_hex is the sha256 of the body.
    REQUIRE(digest::sha256(util::sha256_string(empty_config_body)) ==
            sha_digest(empty_config_hex));

    // empty_config_data is the base64 of the body.
    REQUIRE(util::base64_decode(empty_config_data) ==
            std::optional<std::string>{std::string{empty_config_body}});

    // and the descriptor reports them consistently.
    const auto d = empty_config_descriptor();
    REQUIRE(d.digest == sha_digest(empty_config_hex));
    REQUIRE(d.size == empty_config_body.size());
    REQUIRE(d.data == std::string{empty_config_data});
}

// The serializer must reproduce, byte-for-byte, the image manifest that
// `oras push --artifact-type application/x-squashfs` produced for the public
// prgenv-gnu/24.7:v3 image. Byte identity means the manifest hashes to the same
// digest oras/JFrog recorded, so images we push co-exist with the old tool.
TEST_CASE("serialize squashfs image manifest is oras-identical",
          "[oci][manifest]") {
    manifest m;
    m.artifact_type = std::string{artifact_type_squashfs};
    m.annotations[std::string{annotation_created}] = "2024-08-23T16:00:40Z";
    m.layers.push_back(manifest_layer{
        .media_type = std::string{media_type_layer_tar},
        .digest = sha_digest(image_layer_hex),
        .size = 4046512128,
        .annotations = {{std::string{annotation_title}, "store.squashfs"}}});

    const std::string expected =
        R"({"schemaVersion":2,"mediaType":"application/vnd.oci.image.manifest.v1+json","artifactType":"application/x-squashfs","config":{"mediaType":"application/vnd.oci.empty.v1+json","digest":"sha256:44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a","size":2,"data":"e30="},"layers":[{"mediaType":"application/vnd.oci.image.layer.v1.tar","digest":"sha256:b563b338a3797eb908b1f60a3b710b7021599aaac61d02f11b273bd2ff0986d4","size":4046512128,"annotations":{"org.opencontainers.image.title":"store.squashfs"}}],"annotations":{"org.opencontainers.image.created":"2024-08-23T16:00:40Z"}})";

    auto body = serialize_manifest(m);
    REQUIRE(body == expected);
    // the recorded manifest digest for this image.
    REQUIRE(digest_of(body) == "sha256:" + image_manifest_hex);
}

// Likewise for the `oras attach --artifact-type uenv/meta` referrer manifest.
TEST_CASE("serialize meta referrer manifest is oras-identical",
          "[oci][manifest]") {
    manifest m;
    m.artifact_type = std::string{artifact_type_meta};
    m.annotations[std::string{annotation_created}] = "2024-08-23T16:01:09Z";
    m.layers.push_back(manifest_layer{
        .media_type = std::string{media_type_layer_targz},
        .digest = sha_digest(
            "3e2e44102d0c54bc2b72295b470b994f128a89b1436d567d850dbf131fcc02db"),
        .size = 2366,
        .annotations = {{std::string{annotation_content_digest},
                         "sha256:"
                         "b751bb420ec887b245ea91483849775a8904975aab23b999473b9"
                         "ea07df9571f"},
                        {std::string{annotation_unpack}, "true"},
                        {std::string{annotation_title}, "meta"}}});
    m.subject = descriptor{.media_type = std::string{media_type_manifest},
                           .digest = sha_digest(image_manifest_hex),
                           .size = 588};

    const std::string expected =
        R"({"schemaVersion":2,"mediaType":"application/vnd.oci.image.manifest.v1+json","artifactType":"uenv/meta","config":{"mediaType":"application/vnd.oci.empty.v1+json","digest":"sha256:44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a","size":2,"data":"e30="},"layers":[{"mediaType":"application/vnd.oci.image.layer.v1.tar+gzip","digest":"sha256:3e2e44102d0c54bc2b72295b470b994f128a89b1436d567d850dbf131fcc02db","size":2366,"annotations":{"io.deis.oras.content.digest":"sha256:b751bb420ec887b245ea91483849775a8904975aab23b999473b9ea07df9571f","io.deis.oras.content.unpack":"true","org.opencontainers.image.title":"meta"}}],"subject":{"mediaType":"application/vnd.oci.image.manifest.v1+json","digest":"sha256:b50ca0d101456970ea94dccb5a33adcfd2b9a566bc36c716f4b64a0522089f48","size":588},"annotations":{"org.opencontainers.image.created":"2024-08-23T16:01:09Z"}})";

    auto body = serialize_manifest(m);
    REQUIRE(body == expected);
    REQUIRE(digest_of(body) ==
            "sha256:"
            "f7f04f3b2cf562336c73542f0c53503c3b853ac459f081878843f878955cf267");
}

// parse_manifest is the read counterpart: parsing a serialized manifest must
// recover the same structure (round-trip).
TEST_CASE("parse_manifest round-trips serialize_manifest", "[oci][manifest]") {
    manifest m;
    m.artifact_type = std::string{artifact_type_squashfs};
    m.annotations[std::string{annotation_created}] = "2024-08-23T16:00:40Z";
    m.layers.push_back(manifest_layer{
        .media_type = std::string{media_type_layer_tar},
        .digest = sha_digest(image_layer_hex),
        .size = 4046512128,
        .annotations = {{std::string{annotation_title}, "store.squashfs"}}});

    auto parsed = parse_manifest(serialize_manifest(m));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->artifact_type == artifact_type_squashfs);
    REQUIRE(parsed->config.digest == empty_config_descriptor().digest);
    REQUIRE(parsed->config.data == "e30=");
    REQUIRE(parsed->layers.size() == 1);

    const auto* layer = parsed->find_layer_by_title("store.squashfs");
    REQUIRE(layer != nullptr);
    REQUIRE(layer->digest == sha_digest(image_layer_hex));
    REQUIRE(layer->size == 4046512128);
    REQUIRE_FALSE(parsed->subject.has_value());
    REQUIRE(parsed->annotations.at(std::string{annotation_created}) ==
            "2024-08-23T16:00:40Z");
}

TEST_CASE("parse_manifest finds the unpack layer", "[oci][manifest]") {
    manifest m;
    m.artifact_type = std::string{artifact_type_meta};
    m.layers.push_back(manifest_layer{
        .media_type = std::string{media_type_layer_targz},
        .digest = sha_digest(std::string(64, 'a')),
        .size = 10,
        .annotations = {{std::string{annotation_unpack}, "true"},
                        {std::string{annotation_title}, "meta"}}});

    auto parsed = parse_manifest(serialize_manifest(m));
    REQUIRE(parsed.has_value());
    const auto* layer = parsed->find_unpack_layer();
    REQUIRE(layer != nullptr);
    REQUIRE(layer->wants_unpack());
    REQUIRE(layer->title() == "meta");
}

TEST_CASE("parse_manifest rejects malformed", "[oci][manifest]") {
    REQUIRE_FALSE(parse_manifest("not json").has_value());
    // a layer with an invalid digest is a hard error
    REQUIRE_FALSE(parse_manifest(R"({"layers":[{"digest":"sha256:short"}]})")
                      .has_value());
}

TEST_CASE("parse_manifest survives wrong-typed fields", "[oci][manifest]") {
    // registry responses are untrusted: a key that is present with the wrong
    // type must produce a parse error (or a fallback), never an uncaught
    // nlohmann::json::type_error.
    REQUIRE_FALSE(parse_manifest(R"({"layers":[{"digest":123}]})").has_value());
    REQUIRE_FALSE(parse_manifest(R"({"layers":[1]})").has_value());
    // a wrong-typed config size falls back to 0 (the digest is valid)
    const auto cfg = fmt::format(
        R"({{"config":{{"digest":"sha256:{}","size":"2"}}}})",
        "f7f04f3b2cf562336c73542f0c53503c3b853ac459f081878843f878955cf267");
    auto parsed = parse_manifest(cfg);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->config.size == 0);
    // a wrong-typed top-level mediaType falls back to the manifest media type
    auto media = parse_manifest(R"({"mediaType":123})");
    REQUIRE(media.has_value());
    REQUIRE(media->media_type == media_type_manifest);
}

TEST_CASE("parse_manifest rejects image indexes", "[oci][manifest]") {
    // by media type
    REQUIRE_FALSE(
        parse_manifest(
            R"({"schemaVersion":2,"mediaType":"application/vnd.oci.image.index.v1+json","manifests":[]})")
            .has_value());
    // by docker manifest-list media type
    REQUIRE_FALSE(
        parse_manifest(
            R"({"schemaVersion":2,"mediaType":"application/vnd.docker.distribution.manifest.list.v2+json"})")
            .has_value());
    // by the presence of a `manifests` array even without an index media type
    REQUIRE_FALSE(
        parse_manifest(R"({"schemaVersion":2,"manifests":[]})").has_value());
}

TEST_CASE("serialize image index", "[oci][manifest]") {
    descriptor d{.media_type = std::string{media_type_manifest},
                 .digest = sha_digest(image_manifest_hex),
                 .size = 42,
                 .artifact_type = "uenv/meta"};

    const std::string expected =
        R"({"schemaVersion":2,"mediaType":"application/vnd.oci.image.index.v1+json","manifests":[{"mediaType":"application/vnd.oci.image.manifest.v1+json","digest":"sha256:b50ca0d101456970ea94dccb5a33adcfd2b9a566bc36c716f4b64a0522089f48","size":42,"artifactType":"uenv/meta"}]})";
    REQUIRE(serialize_index({d}) == expected);
}

TEST_CASE("split_registry", "[oci][manifest]") {
    // host with a prefix, no scheme
    auto a = split_registry("jfrog.svc.cscs.ch/uenv");
    REQUIRE(a.has_value());
    REQUIRE(a->base == "https://jfrog.svc.cscs.ch");
    REQUIRE(a->prefix == "uenv");

    // scheme is dropped, https forced
    auto b = split_registry("https://jfrog.svc.cscs.ch/uenv");
    REQUIRE(b.has_value());
    REQUIRE(b->base == "https://jfrog.svc.cscs.ch");
    REQUIRE(b->prefix == "uenv");

    // bare host, no prefix
    auto c = split_registry("registry.example");
    REQUIRE(c.has_value());
    REQUIRE(c->base == "https://registry.example");
    REQUIRE(c->prefix == "");

    // multi-segment prefix, trailing slash trimmed
    auto d = split_registry("host.io/a/b/");
    REQUIRE(d.has_value());
    REQUIRE(d->base == "https://host.io");
    REQUIRE(d->prefix == "a/b");

    // an explicit http scheme is preserved (e.g. a local test registry), and
    // the port is carried through onto the base
    auto e = split_registry("http://127.0.0.1:5000/uenv");
    REQUIRE(e.has_value());
    REQUIRE(e->base == "http://127.0.0.1:5000");
    REQUIRE(e->prefix == "uenv");

    // invalid: empty host / whitespace
    REQUIRE_FALSE(split_registry("").has_value());
    REQUIRE_FALSE(split_registry("/uenv").has_value());
    REQUIRE_FALSE(split_registry("bad host/uenv").has_value());
}

TEST_CASE("repository_path", "[oci][manifest]") {
    REQUIRE(repository_path("uenv", "deploy", "daint", "gh200", "prgenv-gnu",
                            "24.7") ==
            "uenv/deploy/daint/gh200/prgenv-gnu/24.7");
    // empty prefix omits the leading segment
    REQUIRE(repository_path("", "deploy", "daint", "gh200", "prgenv-gnu",
                            "24.7") == "deploy/daint/gh200/prgenv-gnu/24.7");
}
