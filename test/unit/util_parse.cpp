#include <catch2/catch_all.hpp>

#include <util/parse.h>

TEST_CASE("parse unsigned", "[util_parse]") {
    {
        auto result = util::parse_unsigned("0");
        REQUIRE(result);
        REQUIRE(*result == 0u);
    }
    {
        auto result = util::parse_unsigned("42");
        REQUIRE(result);
        REQUIRE(*result == 42u);
    }
    {
        // leading/trailing whitespace is tolerated
        auto result = util::parse_unsigned("  7  ");
        REQUIRE(result);
        REQUIRE(*result == 7u);
    }
}

TEST_CASE("parse unsigned errors", "[util_parse]") {
    REQUIRE_FALSE(util::parse_unsigned(""));
    REQUIRE_FALSE(util::parse_unsigned("a"));
    REQUIRE_FALSE(util::parse_unsigned("-1"));
    REQUIRE_FALSE(util::parse_unsigned("1.5"));
    REQUIRE_FALSE(util::parse_unsigned("1 2"));
    REQUIRE_FALSE(util::parse_unsigned("1garbage"));
}

// a NUL or an invalid byte after the digits is trailing garbage, not the end
// of the input
TEST_CASE("parse unsigned rejects invalid bytes", "[util_parse]") {
    using namespace std::string_literals;
    REQUIRE_FALSE(util::parse_unsigned("12\0"s));
    REQUIRE_FALSE(util::parse_unsigned("12\0garbage"s));
    REQUIRE_FALSE(util::parse_unsigned("12\x80"));
    REQUIRE_FALSE(util::parse_unsigned("\x80"));
}
