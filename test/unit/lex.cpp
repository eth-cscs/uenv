#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <util/lex.h>

TEST_CASE("error characters", "[lex]") {
    for (auto in : {"\\", "~", "'", "\""}) {
        lex::lexer L(in);
        auto t = L.peek();
        REQUIRE(t.kind == lex::tok::error);
        REQUIRE(t.loc == 0u);
    }
}

TEST_CASE("punctuation", "[lex]") {
    lex::lexer L(":,:/@!*!#=");
    REQUIRE(L.next() == lex::token{0, lex::tok::colon, ":"});
    REQUIRE(L.next() == lex::token{1, lex::tok::comma, ","});
    REQUIRE(L.next() == lex::token{2, lex::tok::colon, ":"});
    REQUIRE(L.next() == lex::token{3, lex::tok::slash, "/"});
    REQUIRE(L.next() == lex::token{4, lex::tok::at, "@"});
    REQUIRE(L.next() == lex::token{5, lex::tok::bang, "!"});
    REQUIRE(L.next() == lex::token{6, lex::tok::star, "*"});
    REQUIRE(L.next() == lex::token{7, lex::tok::bang, "!"});
    REQUIRE(L.next() == lex::token{8, lex::tok::hash, "#"});
    REQUIRE(L.next() == lex::token{9, lex::tok::equals, "="});
    // pop the end token twice to check that it does not run off the end
    REQUIRE(L.next() == lex::token{10, lex::tok::end, ""});
    REQUIRE(L.next() == lex::token{10, lex::tok::end, ""});
}

TEST_CASE("number", "[lex]") {
    lex::lexer L("42 42wombat42 42");
    REQUIRE(L.next() == lex::token{0, lex::tok::integer, "42"});
    REQUIRE(L.next() == lex::token{2, lex::tok::whitespace, " "});
    REQUIRE(L.next() == lex::token{3, lex::tok::integer, "42"});
    REQUIRE(L.next() == lex::token{5, lex::tok::symbol, "wombat"});
    REQUIRE(L.next() == lex::token{11, lex::tok::integer, "42"});
    REQUIRE(L.next() == lex::token{13, lex::tok::whitespace, " "});
    REQUIRE(L.next() == lex::token{14, lex::tok::integer, "42"});
    //  pop the end token twice to check that it does not run off the end
    REQUIRE(L.next() == lex::token{16, lex::tok::end, ""});
    REQUIRE(L.next() == lex::token{16, lex::tok::end, ""});
}

TEST_CASE("peek", "[lex]") {
    lex::lexer L(":apple");
    REQUIRE(L.peek() == lex::token{0, lex::tok::colon, ":"});
    REQUIRE(L.peek(1) == lex::token{1, lex::tok::symbol, "apple"});
    REQUIRE(L.peek(2) == lex::token{6, lex::tok::end, ""});
    REQUIRE(L.peek(3) == lex::token{6, lex::tok::end, ""});

    REQUIRE(L.next() == lex::token{0, lex::tok::colon, ":"});
    REQUIRE(L.next() == lex::token{1, lex::tok::symbol, "apple"});
    REQUIRE(L.next() == lex::token{6, lex::tok::end, ""});
}

TEST_CASE("whitespace", "[lex]") {
    lex::lexer L("wombat  soup \n\v");
    REQUIRE(L.next() == lex::token{0, lex::tok::symbol, "wombat"});
    REQUIRE(L.next() == lex::token{6, lex::tok::whitespace, "  "});
    REQUIRE(L.next() == lex::token{8, lex::tok::symbol, "soup"});
    REQUIRE(L.next() == lex::token{12, lex::tok::whitespace, " \n\v"});
}

TEST_CASE("empty input", "[lex]") {
    lex::lexer L("");
    REQUIRE(L.peek() == lex::token{0, lex::tok::end, ""});
    REQUIRE(L.peek(1036) == lex::token{0, lex::tok::end, ""});
    REQUIRE(L.next() == lex::token{0, lex::tok::end, ""});
    REQUIRE(L.next() == lex::token{0, lex::tok::end, ""});
}

TEST_CASE("lex", "[lex]") {
    for (const auto& in : {"prgenv-gnu/ 24.7 :tag,wombat/v2023:lat est",
                           "/opt/images/uenv-x.squashfs,prgenv-gnu"}) {
        lex::lexer L(in);
        while (L.current_kind() != lex::tok::end &&
               L.current_kind() != lex::tok::error) {
            L.next();
        }
        REQUIRE(L.current_kind() == lex::tok::end);
    }
}
