#include <catch2/catch_all.hpp>

#include <oci/digest.h>
#include <oci/reference.h>
#include <util/sha256.h>

using namespace oci;

TEST_CASE("digest parse valid", "[oci][digest]") {
    const std::string hex(64, 'a');
    auto d = digest::parse("sha256:" + hex);
    REQUIRE(d.has_value());
    REQUIRE(d->algorithm() == "sha256");
    REQUIRE(d->hex() == hex);
    REQUIRE(d->string() == "sha256:" + hex);
}

TEST_CASE("digest parse rejects malformed", "[oci][digest]") {
    // missing separator
    REQUIRE_FALSE(digest::parse(std::string(64, 'a')).has_value());
    // unknown algorithm
    REQUIRE_FALSE(digest::parse("md5:" + std::string(32, 'a')).has_value());
    // wrong length for sha256
    REQUIRE_FALSE(digest::parse("sha256:abcd").has_value());
    // uppercase is not lowercase hex
    REQUIRE_FALSE(digest::parse("sha256:" + std::string(64, 'A')).has_value());
    // non-hex character
    REQUIRE_FALSE(
        digest::parse("sha256:" + std::string(63, 'a') + "g").has_value());
    // sha512 length is accepted
    REQUIRE(digest::parse("sha512:" + std::string(128, 'f')).has_value());
}

TEST_CASE("digest from_sha256 and sha256", "[oci][digest]") {
    auto h = util::sha256_string("abc");
    auto d = digest::from_sha256(h);
    REQUIRE(d.string() ==
            "sha256:"
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // the trusted constructor round-trips the same value.
    REQUIRE(digest::sha256(h.hex()) == d);
}

TEST_CASE("digest equality", "[oci][digest]") {
    auto a = digest::parse("sha256:" + std::string(64, 'a')).value();
    auto b = digest::parse("sha256:" + std::string(64, 'a')).value();
    auto c = digest::parse("sha256:" + std::string(64, 'b')).value();
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
}

TEST_CASE("reference tag vs digest", "[oci][reference]") {
    auto t = reference::tag(oci::tag::parse("v3").value());
    REQUIRE(t.is_tag());
    REQUIRE_FALSE(t.is_digest());
    REQUIRE(t.string() == "v3");
    REQUIRE(t.as_tag().string() == "v3");

    auto d = digest::parse("sha256:" + std::string(64, 'a')).value();
    auto r = reference::digest(d);
    REQUIRE(r.is_digest());
    REQUIRE(r.string() == d.string());
    REQUIRE(r.as_digest() == d);
}

TEST_CASE("reference parse", "[oci][reference]") {
    // a digest string parses to a digest reference
    auto r = reference::parse("sha256:" + std::string(64, 'a'));
    REQUIRE(r.has_value());
    REQUIRE(r->is_digest());

    // a plain tag parses to a tag reference
    auto t = reference::parse("v3");
    REQUIRE(t.has_value());
    REQUIRE(t->is_tag());
    REQUIRE(t->string() == "v3");

    // the referrers-tag schema (sha256-<hex>) is a valid tag
    auto rt = reference::parse("sha256-" + std::string(64, 'a'));
    REQUIRE(rt.has_value());
    REQUIRE(rt->is_tag());

    // a bad tag (leading '.') is rejected
    REQUIRE_FALSE(reference::parse(".bad").has_value());
    // empty is rejected
    REQUIRE_FALSE(reference::parse("").has_value());
}
