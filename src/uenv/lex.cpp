/*
prgenv-gnu
prgenv-gnu/24.7
prgenv-gnu/master
prgenv-gnu:v1
prgenv-gnu/24.7:v1
prgenv-gnu/master:v1
prgenv-gnu:rc-2
prgenv-gnu/24.7:rc-2
prgenv-gnu/master:rc-2
prgenv-gnu,prgenv-gnu:rc-2
prgenv-gnu/24.7,prgenv-gnu/24.7:rc-2

/scratch/bob/image.squashfs
/scratch/bob/image.squashfs,prgenv-gnu/24.7:rc-2

symbols:
    , comma
    / slash
    : colon
    string
*/

#include <cctype>

#include <fmt/core.h>

#include "lex.h"

namespace uenv {

inline bool is_alphanumeric(char c) {
    return std::isalnum(static_cast<unsigned char>(c));
}

inline bool is_valid_name_char(char c) {
    switch (c) {
    case '-':
    case '_':
    case '.':
        return true;
    default:
        return is_alphanumeric(c);
    }
}

class lexer_impl {
    const char *begin_;
    const char *stream_;
    token token_;

  public:
    lexer_impl(const char *begin) : begin_(begin), stream_(begin) {
    }

    const token &next(unsigned n = 1) {
        while (n--)
            parse();
        return token_;
    }

  private:
    unsigned loc() const {
        return unsigned(stream_ - begin_);
    }

    bool empty() const {
        return *stream_ == '\0';
    }

    // Consume and return the next token in the stream.
    void parse() {
        using namespace std::string_literals;

        while (!empty()) {
            switch (*stream_) {
            // white space (do we parse this?
            case ' ':
            case '\t':
                ++stream_;
                continue; // skip to next character

            // end of file
            case 0:
                token_ = {loc(), tok::eof, "eof"s};
                return;
            case ':':
                token_ = {loc(), tok::colon, ":"};
                ++stream_;
                return;
            case ',':
                token_ = {loc(), tok::comma, ","};
                ++stream_;
                return;
            case '/':
                token_ = {loc(), tok::slash, "/"};
                ++stream_;
                return;

            case 'a' ... 'z':
            case 'A' ... 'Z':
            case '0' ... '9':
            case '-':
            case '_':
                token_ = name();
                return;
            default:
                token_ = {loc(), tok::error, std::string("Unexpected character '") + character() + "'"};
                return;
            }
        }

        if (!empty()) {
            token_ = {loc(), tok::error, "Internal lexer error: expected end of input, please open a bug report"s};
            return;
        }
        token_ = {loc(), tok::eof, "eof"s};
        return;
    }

    char character() {
        return *(stream_++);
    }

    token name() {
        using namespace std::string_literals;
        auto start = loc();
        std::string name;
        char c = *stream_;

        // Assert that current position is at the start of an identifier
        if (!(is_valid_name_char(c))) {
            return {start, tok::error,
                    "Internal error: lexer attempting to read identifier when none is available '.'"s};
        }

        name += c;
        ++stream_;
        while (1) {
            c = *stream_;

            if (is_valid_name_char(c)) {
                name += c;
                ++stream_;
            } else {
                break;
            }
        }

        return {start, tok::name, std::move(name)};
    }
};

lexer::lexer(const char *begin) : impl_(new lexer_impl(begin)) {
}

const token &lexer::next(unsigned n) {
    return impl_->next(n);
}

lexer::~lexer() = default;

} // namespace uenv
