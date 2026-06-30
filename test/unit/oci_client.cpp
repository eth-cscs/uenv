#include <catch2/catch_all.hpp>

#include <oci/client.h>
#include <util/sha256.h>

using namespace oci;

TEST_CASE("oci digest_string", "[oci][client]") {
    auto d = util::sha256_string("abc");
    REQUIRE(digest_string(d) ==
            "sha256:"
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

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
    REQUIRE(resolve_upload_url(reg, "/v2/r/blobs/uploads/abc?_state=xyz", dig) ==
            "https://jfrog.svc.cscs.ch/v2/r/blobs/uploads/abc?_state=xyz&digest="
            "sha256:deadbeef");

    // absolute Location (e.g. redirected to storage) is used verbatim
    REQUIRE(resolve_upload_url(reg, "https://store.example/up/abc?sig=1", dig) ==
            "https://store.example/up/abc?sig=1&digest=sha256:deadbeef");

    // trailing slash on the registry base is not doubled
    REQUIRE(resolve_upload_url("https://reg/", "/v2/x", dig) ==
            "https://reg/v2/x?digest=sha256:deadbeef");
}

TEST_CASE("oci parse_tags_list", "[oci][client]") {
    auto tags =
        parse_tags_list(R"({"name":"r","tags":["v1","v2","latest"]})");
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
    const auto body = R"({
        "schemaVersion": 2,
        "mediaType": "application/vnd.oci.image.index.v1+json",
        "manifests": [
            {
                "mediaType": "application/vnd.oci.image.manifest.v1+json",
                "digest": "sha256:f7f04f3b",
                "size": 1234,
                "artifactType": "application/vnd.cscs.uenv.meta"
            }
        ]
    })";
    auto refs = parse_referrers(body);
    REQUIRE(refs.has_value());
    REQUIRE(refs->size() == 1);
    REQUIRE((*refs)[0].digest == "sha256:f7f04f3b");
    REQUIRE((*refs)[0].size == 1234);
    REQUIRE((*refs)[0].artifact_type == "application/vnd.cscs.uenv.meta");

    // no manifests array -> empty list
    auto empty = parse_referrers(R"({"manifests":[]})");
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());

    REQUIRE(parse_referrers("not json") == std::nullopt);
}
