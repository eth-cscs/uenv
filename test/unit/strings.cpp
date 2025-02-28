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
