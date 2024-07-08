#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/lex.h>

TEST_CASE("lex", "[symbols]") {
    uenv::lexer L(":,:/");
    {
        auto t = L.next();
        fmt::print("{}\n", t);
        REQUIRE(t.kind == uenv::tok::colon);
        REQUIRE(t.loc == 0u);
    }
    {
        auto t = L.next();
        fmt::print("{}\n", t);
        REQUIRE(t.kind == uenv::tok::comma);
        REQUIRE(t.loc == 1u);
    }
    {
        auto t = L.next();
        fmt::print("{}\n", t);
        REQUIRE(t.kind == uenv::tok::colon);
        REQUIRE(t.loc == 2u);
    }
    {
        auto t = L.next();
        fmt::print("{}\n", t);
        REQUIRE(t.kind == uenv::tok::slash);
        REQUIRE(t.loc == 3u);
    }
    // pop the eof token twice to check that it does not run off the end
    {
        auto t = L.next();
        fmt::print("{}\n", t);
        REQUIRE(t.kind == uenv::tok::eof);
    }
    {
        auto t = L.next();
        fmt::print("{}\n", t);
        REQUIRE(t.kind == uenv::tok::eof);
    }
}

TEST_CASE("lex", "[parse]") {
    for (const auto &in : {"prgenv-gnu/ 24.7 :tag,wombat/v2023:lat est", "/opt/images/uenv-x.squashfs,prgenv-gnu"}) {
        fmt::print("=== '{}'\n", in);
        uenv::lexer L(in);
        auto tok = L.next();
        do {
            fmt::print("{}\n", tok);
            tok = L.next();
        } while (tok.kind != uenv::tok::eof);
    }
}
