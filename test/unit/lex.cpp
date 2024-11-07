#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/lex.h>

TEST_CASE("error characters", "[lex]") {
    for (auto in : {"\\", "~", "'", "\""}) {
        uenv::lexer L(in);
        auto t = L.peek();
        REQUIRE(t.kind == uenv::tok::error);
        REQUIRE(t.loc == 0u);
    }
}

TEST_CASE("punctuation", "[lex]") {
    uenv::lexer L(":,:/@!*!");
    REQUIRE(L.next() == uenv::token{0, uenv::tok::colon, ":"});
    REQUIRE(L.next() == uenv::token{1, uenv::tok::comma, ","});
    REQUIRE(L.next() == uenv::token{2, uenv::tok::colon, ":"});
    REQUIRE(L.next() == uenv::token{3, uenv::tok::slash, "/"});
    REQUIRE(L.next() == uenv::token{4, uenv::tok::at, "@"});
    REQUIRE(L.next() == uenv::token{5, uenv::tok::bang, "!"});
    REQUIRE(L.next() == uenv::token{6, uenv::tok::star, "*"});
    REQUIRE(L.next() == uenv::token{7, uenv::tok::bang, "!"});
    // pop the end token twice to check that it does not run off the end
    REQUIRE(L.next() == uenv::token{8, uenv::tok::end, ""});
    REQUIRE(L.next() == uenv::token{8, uenv::tok::end, ""});
}

TEST_CASE("number", "[lex]") {
    uenv::lexer L("42 42wombat42 42");
    REQUIRE(L.next() == uenv::token{0, uenv::tok::integer, "42"});
    REQUIRE(L.next() == uenv::token{2, uenv::tok::whitespace, " "});
    REQUIRE(L.next() == uenv::token{3, uenv::tok::integer, "42"});
    REQUIRE(L.next() == uenv::token{5, uenv::tok::symbol, "wombat"});
    REQUIRE(L.next() == uenv::token{11, uenv::tok::integer, "42"});
    REQUIRE(L.next() == uenv::token{13, uenv::tok::whitespace, " "});
    REQUIRE(L.next() == uenv::token{14, uenv::tok::integer, "42"});
    //  pop the end token twice to check that it does not run off the end
    REQUIRE(L.next() == uenv::token{16, uenv::tok::end, ""});
    REQUIRE(L.next() == uenv::token{16, uenv::tok::end, ""});
}

TEST_CASE("peek", "[lex]") {
    uenv::lexer L(":apple");
    REQUIRE(L.peek() == uenv::token{0, uenv::tok::colon, ":"});
    REQUIRE(L.peek(1) == uenv::token{1, uenv::tok::symbol, "apple"});
    REQUIRE(L.peek(2) == uenv::token{6, uenv::tok::end, ""});
    REQUIRE(L.peek(3) == uenv::token{6, uenv::tok::end, ""});

    REQUIRE(L.next() == uenv::token{0, uenv::tok::colon, ":"});
    REQUIRE(L.next() == uenv::token{1, uenv::tok::symbol, "apple"});
    REQUIRE(L.next() == uenv::token{6, uenv::tok::end, ""});
}

TEST_CASE("whitespace", "[lex]") {
    uenv::lexer L("wombat  soup \n\v");
    REQUIRE(L.next() == uenv::token{0, uenv::tok::symbol, "wombat"});
    REQUIRE(L.next() == uenv::token{6, uenv::tok::whitespace, "  "});
    REQUIRE(L.next() == uenv::token{8, uenv::tok::symbol, "soup"});
    REQUIRE(L.next() == uenv::token{12, uenv::tok::whitespace, " \n\v"});
}

TEST_CASE("empty input", "[lex]") {
    uenv::lexer L("");
    REQUIRE(L.peek() == uenv::token{0, uenv::tok::end, ""});
    REQUIRE(L.peek(1036) == uenv::token{0, uenv::tok::end, ""});
    REQUIRE(L.next() == uenv::token{0, uenv::tok::end, ""});
    REQUIRE(L.next() == uenv::token{0, uenv::tok::end, ""});
}

TEST_CASE("lex", "[lex]") {
    for (const auto& in : {"prgenv-gnu/ 24.7 :tag,wombat/v2023:lat est",
                           "/opt/images/uenv-x.squashfs,prgenv-gnu"}) {
        uenv::lexer L(in);
        while (L.current_kind() != uenv::tok::end &&
               L.current_kind() != uenv::tok::error) {
            L.next();
        }
        REQUIRE(L.current_kind() == uenv::tok::end);
    }
}
