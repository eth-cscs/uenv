#include <catch2/catch_all.hpp>

#include <fmt/format.h>

#include <oci/client.h>
#include <oci/digest.h>
#include <oci/util.h>
#include <util/sha256.h>

using namespace oci;
using namespace oci::detail;

TEST_CASE("oci path builders", "[oci][client]") {
    const std::string repo = "deploy/todi/gh200/app/1.0";
    REQUIRE(blob_path(repo, "sha256:abc") ==
            "/v2/deploy/todi/gh200/app/1.0/blobs/sha256:abc");
    REQUIRE(manifest_path(repo, "v1") ==
            "/v2/deploy/todi/gh200/app/1.0/manifests/v1");
    REQUIRE(uploads_path(repo) ==
            "/v2/deploy/todi/gh200/app/1.0/blobs/uploads/");
    REQUIRE(tags_path(repo) == "/v2/deploy/todi/gh200/app/1.0/tags/list");
    REQUIRE(referrers_path(repo, "sha256:abc") ==
            "/v2/deploy/todi/gh200/app/1.0/referrers/sha256:abc");
}

TEST_CASE("oci resolve_upload_url", "[oci][client]") {
    const std::string reg = "https://jfrog.svc.cscs.ch";
    const std::string dig = "sha256:deadbeef";

    // registry-relative Location, no existing query -> '?'
    REQUIRE(resolve_upload_url(reg, "/v2/r/blobs/uploads/abc", dig) ==
            "https://jfrog.svc.cscs.ch/v2/r/blobs/uploads/abc?digest=sha256:"
            "deadbeef");

    // relative Location that already carries a query -> '&'
    REQUIRE(
        resolve_upload_url(reg, "/v2/r/blobs/uploads/abc?_state=xyz", dig) ==
        "https://jfrog.svc.cscs.ch/v2/r/blobs/uploads/abc?_state=xyz&digest="
        "sha256:deadbeef");

    // absolute Location (e.g. redirected to storage) is used verbatim
    REQUIRE(
        resolve_upload_url(reg, "https://store.example/up/abc?sig=1", dig) ==
        "https://store.example/up/abc?sig=1&digest=sha256:deadbeef");

    // trailing slash on the registry base is not doubled
    REQUIRE(resolve_upload_url("https://reg/", "/v2/x", dig) ==
            "https://reg/v2/x?digest=sha256:deadbeef");
}

TEST_CASE("oci parse_tags_list", "[oci][client]") {
    auto tags = parse_tags_list(R"({"name":"r","tags":["v1","v2","latest"]})");
    REQUIRE(tags.has_value());
    REQUIRE(*tags == std::vector<std::string>{"v1", "v2", "latest"});

    // empty/absent tag list is valid (returns empty, not an error)
    auto none = parse_tags_list(R"({"name":"r","tags":null})");
    REQUIRE(none.has_value());
    REQUIRE(none->empty());

    REQUIRE(parse_tags_list("not json") == std::nullopt);
}

TEST_CASE("oci parse_referrers", "[oci][client]") {
    // an image-index body, as returned by /v2/<repo>/referrers/<digest>
    const auto digest_hex =
        "f7f04f3b2cf562336c73542f0c53503c3b853ac459f081878843f878955cf267";
    const auto body = fmt::format(R"({{
        "schemaVersion": 2,
        "mediaType": "application/vnd.oci.image.index.v1+json",
        "manifests": [
            {{
                "mediaType": "application/vnd.oci.image.manifest.v1+json",
                "digest": "sha256:{}",
                "size": 1234,
                "artifactType": "application/vnd.cscs.uenv.meta"
            }}
        ]
    }})",
                                  digest_hex);
    auto refs = parse_referrers(body);
    REQUIRE(refs.has_value());
    REQUIRE(refs->size() == 1);
    REQUIRE((*refs)[0].digest == digest::sha256(digest_hex));
    REQUIRE((*refs)[0].size == 1234);
    REQUIRE((*refs)[0].artifact_type == "application/vnd.cscs.uenv.meta");

    // an entry with a malformed digest is skipped
    auto skipped = parse_referrers(
        R"({"manifests":[{"digest":"sha256:short","size":1}]})");
    REQUIRE(skipped.has_value());
    REQUIRE(skipped->empty());

    // wrong-typed fields never throw: a non-string digest skips the entry,
    // and a non-integer size falls back to 0
    auto wrong_digest = parse_referrers(R"({"manifests":[{"digest":123}]})");
    REQUIRE(wrong_digest.has_value());
    REQUIRE(wrong_digest->empty());
    auto wrong_size = parse_referrers(
        fmt::format(R"({{"manifests":[{{"digest":"sha256:{}","size":"5"}}]}})",
                    digest_hex));
    REQUIRE(wrong_size.has_value());
    REQUIRE(wrong_size->size() == 1);
    REQUIRE(wrong_size->front().size == 0);

    // no manifests array -> empty list
    auto empty = parse_referrers(R"({"manifests":[]})");
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());

    REQUIRE(parse_referrers("not json") == std::nullopt);
}
