#include <catch2/catch_all.hpp>

#include <unordered_map>

#include <fmt/format.h>

#include <util/sha.h>

using namespace util;

TEST_CASE("is_sha_string", "[sha]") {
    REQUIRE(is_sha_string(""));
    REQUIRE(is_sha_string("0123456789abcdef"));
    REQUIRE_FALSE(is_sha_string("ABCDEF")); // uppercase is not lowercase hex
    REQUIRE_FALSE(is_sha_string("g"));      // out of range
    REQUIRE_FALSE(is_sha_string("12 34"));  // whitespace
}

TEST_CASE("sha256 parse accepts exactly 64 lowercase hex", "[sha]") {
    const std::string hex(64, 'a');
    auto s = sha256::parse(hex);
    REQUIRE(s.has_value());
    REQUIRE(s->string() == hex);
}

TEST_CASE("uenv_id parse accepts exactly 16 lowercase hex", "[sha]") {
    const std::string hex(16, 'b');
    auto s = uenv_id::parse(hex);
    REQUIRE(s.has_value());
    REQUIRE(s->string() == hex);
}

TEST_CASE("sha256 parse rejects malformed", "[sha]") {
    // wrong length
    REQUIRE_FALSE(sha256::parse(std::string(63, 'a')).has_value());
    REQUIRE_FALSE(sha256::parse(std::string(65, 'a')).has_value());
    REQUIRE_FALSE(sha256::parse("").has_value());
    // uppercase is not lowercase hex
    REQUIRE_FALSE(sha256::parse(std::string(64, 'A')).has_value());
    // non-hex character
    REQUIRE_FALSE(sha256::parse(std::string(63, 'a') + "g").has_value());
}

TEST_CASE("sha default is a valid all-zero value", "[sha]") {
    sha256 s;
    REQUIRE(s.string() == std::string(64, '0'));
}

TEST_CASE("sha equality and ordering", "[sha]") {
    auto a = sha256::parse(std::string(64, 'a')).value();
    auto b = sha256::parse(std::string(64, 'a')).value();
    auto c = sha256::parse(std::string(64, 'c')).value();
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
    REQUIRE(a < c);
}

TEST_CASE("sha is hashable and formattable", "[sha]") {
    const std::string hex(64, 'd');
    auto s = sha256::parse(hex).value();

    // usable as an unordered_map key (exercises std::hash specialisation).
    std::unordered_map<sha256, int> m;
    m[s] = 7;
    REQUIRE(m.at(s) == 7);

    // fmt formatter renders the text form.
    REQUIRE(fmt::format("{}", s) == hex);
}
