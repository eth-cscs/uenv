#include <string_view>

#include <util/lex.h>

namespace lex {

bool operator==(const token& lhs, const token& rhs) {
    return lhs.loc == rhs.loc && lhs.kind == rhs.kind &&
           lhs.spelling == rhs.spelling;
};

bool operator==(const token& lhs, tok rhs) {
    return lhs.kind == rhs;
}

// the character classes are ascii-only and independent of the process locale.
// every input lexed here (labels, paths, urls, http headers) is ascii by
// specification, and std::isalpha() and friends classify bytes above 0x7f
// differently depending on the locale of the host process.
inline bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

inline bool is_valid_symbol(char c) {
    return c == '_' || is_alpha(c);
}

// the whitespace characters that parse() dispatches to whitespace()
inline bool is_space(char c) {
    switch (c) {
    case ' ':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
    case '\v':
        return true;
    default:
        return false;
    }
}

class lexer_impl {
    std::string_view input_;
    std::string_view::iterator stream_;
    token token_;

  public:
    lexer_impl(std::string_view input)
        : input_(input), stream_(input_.begin()) {
        parse();
    }

    std::string_view string() const {
        return input_;
    }

    token next() {
        auto t = token_;
        parse();
        return t;
    }

    token peek(unsigned int n = 1) {
        auto current_stream = stream_;
        auto current_token = token_;

        while (n--) {
            parse();
        }

        std::swap(token_, current_token);
        stream_ = current_stream;

        return current_token;
    }

    void seek(unsigned pos) {
        if (pos > input_.size()) {
            pos = input_.size();
        }
        stream_ = input_.begin() + pos;
        parse();
    }

    tok current_kind() {
        return token_.kind;
    }

    bool operator==(tok t) const {
        return token_.kind == t;
    }

  private:
    unsigned loc() const {
        return unsigned(stream_ - input_.begin());
    }

    bool empty() const {
        return stream_ == input_.end();
    }

    // Consume and return the next token in the stream.
    void parse() {
        while (!empty()) {
            switch (*stream_) {
            case ' ':  // space
            case '\f': // form feed
            case '\n': // newline
            case '\r': // carriage return
            case '\t': // tab
            case '\v': // vertical tab
                token_ = whitespace();
                return;

            case ':':
                character_token(tok::colon);
                ++stream_;
                return;
            case ',':
                character_token(tok::comma);
                ++stream_;
                return;
            case '.':
                character_token(tok::dot);
                ++stream_;
                return;
            case '-':
                character_token(tok::dash);
                ++stream_;
                return;
            case '/':
                character_token(tok::slash);
                ++stream_;
                return;
            case '=':
                character_token(tok::equals);
                ++stream_;
                return;
            case '#':
                character_token(tok::hash);
                ++stream_;
                return;
            case '@':
                character_token(tok::at);
                ++stream_;
                return;
            case '!':
                character_token(tok::bang);
                ++stream_;
                return;
            case '%':
                character_token(tok::percent);
                ++stream_;
                return;
            case '*':
                character_token(tok::star);
                ++stream_;
                return;
            case '+':
                character_token(tok::plus);
                ++stream_;
                return;
            case '?':
                character_token(tok::question);
                ++stream_;
                return;
            case '&':
                character_token(tok::amp);
                ++stream_;
                return;
            case '~':
                character_token(tok::tilde);
                ++stream_;
                return;
            case '[':
                character_token(tok::lbracket);
                ++stream_;
                return;
            case ']':
                character_token(tok::rbracket);
                ++stream_;
                return;
            case '$':
                character_token(tok::dollar);
                ++stream_;
                return;
            case ';':
                character_token(tok::semicolon);
                ++stream_;
                return;
            case '(':
                character_token(tok::lparen);
                ++stream_;
                return;
            case ')':
                character_token(tok::rparen);
                ++stream_;
                return;
            case '\'':
                character_token(tok::squote);
                ++stream_;
                return;
            case '"':
                character_token(tok::dquote);
                ++stream_;
                return;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
            case 'a' ... 'z':
            case 'A' ... 'Z':
            case '_':
                token_ = symbol();
                return;
            case '0' ... '9':
                token_ = integer();
                return;
#pragma GCC diagnostic pop
            default:
                // every other byte, including NUL and all non-ascii bytes,
                // is a one character error token. it is consumed like any
                // other token so that the lexer always makes progress: the
                // parsers loop on token kinds, and a token that is returned
                // again and again without the stream advancing is an
                // infinite loop for every one of them.
                character_token(tok::error);
                ++stream_;
                return;
            }
        }

        token_ = {loc(), tok::end, input_.substr(input_.size())};
    }

    // a single character token at the current position
    void character_token(tok kind) {
        token_ = {loc(), kind, input_.substr(loc(), 1)};
    }

    // the loops below must test empty() before dereferencing stream_: the
    // input is a string_view, which has no terminator to stop on.
    token symbol() {
        const auto start_loc = loc();
        const auto start = stream_;

        while (!empty() && is_valid_symbol(*stream_)) {
            ++stream_;
        }

        return {start_loc, tok::symbol,
                std::string_view(&(*start), std::distance(start, stream_))};
    }

    token integer() {
        const auto start_loc = loc();
        const auto start = stream_;

        while (!empty() && is_digit(*stream_)) {
            ++stream_;
        }

        return {start_loc, tok::integer,
                std::string_view(&(*start), std::distance(start, stream_))};
    }

    token whitespace() {
        const auto start_loc = loc();
        const auto start = stream_;

        while (!empty() && is_space(*stream_)) {
            ++stream_;
        }

        return {start_loc, tok::whitespace,
                std::string_view(&(*start), std::distance(start, stream_))};
    }
};

lexer::lexer(std::string_view input) : impl_(new lexer_impl(input)) {
}

token lexer::next() {
    return impl_->next();
}

token lexer::peek(unsigned n) {
    return impl_->peek(n);
}

void lexer::seek(unsigned pos) {
    impl_->seek(pos);
}

tok lexer::current_kind() const {
    return impl_->current_kind();
}

std::string_view lexer::string() const {
    return impl_->string();
}

// return true if the current token matches tok
bool lexer::operator==(tok t) const {
    return impl_->operator==(t);
}

lexer::~lexer() = default;

} // namespace lex
