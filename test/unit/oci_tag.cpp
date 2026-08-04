#include <catch2/catch_all.hpp>

#include <oci/tag.h>

using namespace oci;

TEST_CASE("tag parse valid", "[oci][tag]") {
    // a plain tag
    auto t = tag::parse("v1");
    REQUIRE(t.has_value());
    REQUIRE(t->string() == "v1");

    // dots and underscores are part of the grammar
    REQUIRE(tag::parse("24.7_v2").has_value());
    REQUIRE(tag::parse("24.7_v2")->string() == "24.7_v2");

    // the referrers-index schema (<algo>-<hex>) is a valid tag
    const auto referrers = "sha256-" + std::string(64, 'a');
    auto rt = tag::parse(referrers);
    REQUIRE(rt.has_value());
    REQUIRE(rt->string() == referrers);

    // a 128-character tag is the maximum length accepted
    REQUIRE(tag::parse(std::string(128, 'a')).has_value());
}

TEST_CASE("tag parse rejects malformed", "[oci][tag]") {
    // a leading '.' or '-' is forbidden by the grammar
    REQUIRE_FALSE(tag::parse(".bad").has_value());
    REQUIRE_FALSE(tag::parse("-bad").has_value());
    // empty is rejected
    REQUIRE_FALSE(tag::parse("").has_value());
    // over-long (>128 characters) is rejected
    REQUIRE_FALSE(tag::parse(std::string(129, 'a')).has_value());
    // an out-of-grammar character is rejected
    REQUIRE_FALSE(tag::parse("a/b").has_value());
}

TEST_CASE("tag equality", "[oci][tag]") {
    auto a = tag::parse("v1").value();
    auto b = tag::parse("v1").value();
    auto c = tag::parse("v2").value();
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
}
