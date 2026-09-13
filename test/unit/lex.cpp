#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <util/lex.h>

TEST_CASE("error characters", "[lex]") {
    for (auto in : {"\\", "{", "}", "|", "^", "<", ">", "`"}) {
        lex::lexer L(in);
        auto t = L.peek();
        REQUIRE(t.kind == lex::tok::error);
        REQUIRE(t.loc == 0u);
    }
}

TEST_CASE("url and quote punctuation", "[lex]") {
    lex::lexer L("?&~[]$;()'\"");
    REQUIRE(L.next() == lex::token{0, lex::tok::question, "?"});
    REQUIRE(L.next() == lex::token{1, lex::tok::amp, "&"});
    REQUIRE(L.next() == lex::token{2, lex::tok::tilde, "~"});
    REQUIRE(L.next() == lex::token{3, lex::tok::lbracket, "["});
    REQUIRE(L.next() == lex::token{4, lex::tok::rbracket, "]"});
    REQUIRE(L.next() == lex::token{5, lex::tok::dollar, "$"});
    REQUIRE(L.next() == lex::token{6, lex::tok::semicolon, ";"});
    REQUIRE(L.next() == lex::token{7, lex::tok::lparen, "("});
    REQUIRE(L.next() == lex::token{8, lex::tok::rparen, ")"});
    REQUIRE(L.next() == lex::token{9, lex::tok::squote, "'"});
    REQUIRE(L.next() == lex::token{10, lex::tok::dquote, "\""});
    REQUIRE(L.next() == lex::token{11, lex::tok::end, ""});
}

TEST_CASE("seek", "[lex]") {
    lex::lexer L("abc?def");
    REQUIRE(L.peek() == lex::token{0, lex::tok::symbol, "abc"});
    // seek onto the 'def' symbol (offset 4)
    L.seek(4);
    REQUIRE(L.peek() == lex::token{4, lex::tok::symbol, "def"});
    REQUIRE(L.next() == lex::token{4, lex::tok::symbol, "def"});
    REQUIRE(L.current_kind() == lex::tok::end);
    // seeking to the end yields the end token
    L.seek(7);
    REQUIRE(L.current_kind() == lex::tok::end);
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

// every byte value lexes to exactly one token of the expected kind followed by
// end: nothing is skipped, nothing is sticky, and bytes outside the ascii
// alphabet of the grammar (including NUL and everything above 0x7f) are
// one-character error tokens rather than a stop or a hang.
TEST_CASE("every byte value", "[lex]") {
    using lex::tok;
    for (unsigned b = 0; b < 256; ++b) {
        const char c = static_cast<char>(b);
        tok expected = tok::error;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            expected = tok::symbol;
        } else if (c >= '0' && c <= '9') {
            expected = tok::integer;
        } else {
            switch (c) {
            case ' ':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
            case '\v':
                expected = tok::whitespace;
                break;
            case ':':
                expected = tok::colon;
                break;
            case ',':
                expected = tok::comma;
                break;
            case '.':
                expected = tok::dot;
                break;
            case '-':
                expected = tok::dash;
                break;
            case '/':
                expected = tok::slash;
                break;
            case '=':
                expected = tok::equals;
                break;
            case '#':
                expected = tok::hash;
                break;
            case '@':
                expected = tok::at;
                break;
            case '!':
                expected = tok::bang;
                break;
            case '%':
                expected = tok::percent;
                break;
            case '*':
                expected = tok::star;
                break;
            case '+':
                expected = tok::plus;
                break;
            case '?':
                expected = tok::question;
                break;
            case '&':
                expected = tok::amp;
                break;
            case '~':
                expected = tok::tilde;
                break;
            case '[':
                expected = tok::lbracket;
                break;
            case ']':
                expected = tok::rbracket;
                break;
            case '$':
                expected = tok::dollar;
                break;
            case ';':
                expected = tok::semicolon;
                break;
            case '(':
                expected = tok::lparen;
                break;
            case ')':
                expected = tok::rparen;
                break;
            case '\'':
                expected = tok::squote;
                break;
            case '"':
                expected = tok::dquote;
                break;
            default:
                break;
            }
        }
        const std::string_view input(&c, 1);
        lex::lexer L(input);
        INFO("byte 0x" << std::hex << b);
        const auto t = L.next();
        REQUIRE(t.kind == expected);
        REQUIRE(t.loc == 0u);
        REQUIRE(t.spelling == input);
        REQUIRE(L.next() == lex::token{1, tok::end, ""});
    }
}

// an invalid byte is consumed, so the token after it is reached: a parser
// that loops on token kinds must always see the stream advance.
TEST_CASE("error tokens advance", "[lex]") {
    lex::lexer L("a\\b|\x80\xff");
    REQUIRE(L.next() == lex::token{0, lex::tok::symbol, "a"});
    REQUIRE(L.next() == lex::token{1, lex::tok::error, "\\"});
    REQUIRE(L.next() == lex::token{2, lex::tok::symbol, "b"});
    REQUIRE(L.next() == lex::token{3, lex::tok::error, "|"});
    REQUIRE(L.next() == lex::token{4, lex::tok::error, "\x80"});
    REQUIRE(L.next() == lex::token{5, lex::tok::error, "\xff"});
    REQUIRE(L.next() == lex::token{6, lex::tok::end, ""});
}

// a NUL byte inside the input is an invalid character, not the end of the
// input: "abc\0xyz" must not silently parse as "abc".
TEST_CASE("embedded NUL", "[lex]") {
    const std::string input("ab\0cd", 5);
    lex::lexer L(input);
    REQUIRE(L.next() == lex::token{0, lex::tok::symbol, "ab"});
    REQUIRE(L.next() ==
            lex::token{2, lex::tok::error, std::string_view("\0", 1)});
    REQUIRE(L.next() == lex::token{3, lex::tok::symbol, "cd"});
    REQUIRE(L.next() == lex::token{5, lex::tok::end, ""});
}

// the input is a string_view with no terminator: a symbol, integer or
// whitespace run at the end of the view must stop at the view's end rather than
// reading (and spelling) whatever follows in memory.
TEST_CASE("unterminated input", "[lex]") {
    {
        lex::lexer L(std::string_view("wombats", 3));
        REQUIRE(L.next() == lex::token{0, lex::tok::symbol, "wom"});
        REQUIRE(L.next() == lex::token{3, lex::tok::end, ""});
    }
    {
        lex::lexer L(std::string_view("123456", 3));
        REQUIRE(L.next() == lex::token{0, lex::tok::integer, "123"});
        REQUIRE(L.next() == lex::token{3, lex::tok::end, ""});
    }
    {
        lex::lexer L(std::string_view("    x", 2));
        REQUIRE(L.next() == lex::token{0, lex::tok::whitespace, "  "});
        REQUIRE(L.next() == lex::token{2, lex::tok::end, ""});
    }
}
