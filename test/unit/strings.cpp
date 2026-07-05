#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include <util/strings.h>

TEST_CASE("strip", "[strings]") {
    REQUIRE(util::strip("wombat") == "wombat");
    REQUIRE(util::strip("wombat soup") == "wombat soup");
    REQUIRE(util::strip("wombat-soup") == "wombat-soup");
    REQUIRE(util::strip("wombat \nsoup") == "wombat \nsoup");
    REQUIRE(util::strip("") == "");
    REQUIRE(util::strip(" ") == "");
    REQUIRE(util::strip(" x") == "x");
    REQUIRE(util::strip("x ") == "x");
    REQUIRE(util::strip(" x ") == "x");
    REQUIRE(util::strip(" \n\f  ") == "");
    REQUIRE(util::strip(" wombat") == "wombat");
    REQUIRE(util::strip("wombat \n") == "wombat");
    REQUIRE(util::strip("\t\f\vwombat \n") == "wombat");
}

TEST_CASE("split", "[strings]") {
    using v = std::vector<std::string>;
    REQUIRE(util::split("", ',') == v{""});
    REQUIRE(util::split(",", ',') == v{"", ""});
    REQUIRE(util::split(",,", ',') == v{"", "", ""});
    REQUIRE(util::split(",a", ',') == v{"", "a"});
    REQUIRE(util::split("a,", ',') == v{"a", ""});
    REQUIRE(util::split("a", ',') == v{"a"});
    REQUIRE(util::split("a,b", ',') == v{"a", "b"});
    REQUIRE(util::split("a,b,c", ',') == v{"a", "b", "c"});
    REQUIRE(util::split("a,b,,c", ',') == v{"a", "b", "", "c"});

    REQUIRE(util::split("", ',', true) == v{});
    REQUIRE(util::split(",", ',', true) == v{});
    REQUIRE(util::split(",,", ',', true) == v{});
    REQUIRE(util::split(",a", ',', true) == v{"a"});
    REQUIRE(util::split("a,", ',', true) == v{"a"});
    REQUIRE(util::split("a", ',', true) == v{"a"});
    REQUIRE(util::split("a,b", ',', true) == v{"a", "b"});
    REQUIRE(util::split("a,b,c", ',', true) == v{"a", "b", "c"});
    REQUIRE(util::split("a,b,,c", ',', true) == v{"a", "b", "c"});
}

TEST_CASE("base64_decode", "[strings]") {
    auto dec = [](std::string_view s) { return util::base64_decode(s); };

    // RFC 4648 test vectors
    REQUIRE(dec("") == std::optional<std::string>{""});
    REQUIRE(dec("Zg==") == std::optional<std::string>{"f"});
    REQUIRE(dec("Zm8=") == std::optional<std::string>{"fo"});
    REQUIRE(dec("Zm9v") == std::optional<std::string>{"foo"});
    REQUIRE(dec("Zm9vYg==") == std::optional<std::string>{"foob"});
    REQUIRE(dec("Zm9vYmE=") == std::optional<std::string>{"fooba"});
    REQUIRE(dec("Zm9vYmFy") == std::optional<std::string>{"foobar"});

    // a docker-style user:pass token
    REQUIRE(dec("YWxpY2U6czNjcmV0") == std::optional<std::string>{"alice:s3cret"});

    // padding is optional; embedded whitespace/newlines are ignored
    REQUIRE(dec("Zm9v\n") == std::optional<std::string>{"foo"});
    REQUIRE(dec("Zg") == std::optional<std::string>{"f"});

    // characters outside the standard alphabet are rejected
    REQUIRE(dec("****") == std::nullopt);
    REQUIRE(dec("ab_c") == std::nullopt); // url-safe alphabet is not accepted
}
