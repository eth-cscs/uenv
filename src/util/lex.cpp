#include <cctype>
#include <string_view>

#include <util/lex.h>

namespace lex {

bool operator==(const token& lhs, const token& rhs) {
    return lhs.loc == rhs.loc && lhs.kind == rhs.kind &&
           lhs.spelling == rhs.spelling;
};

inline bool is_alphanumeric(char c) {
    return std::isalnum(static_cast<unsigned char>(c));
}

inline bool is_alpha(char c) {
    return std::isalpha(static_cast<unsigned char>(c));
}

inline bool is_valid_symbol(char c) {
    switch (c) {
    case '_':
        return true;
    default:
        return is_alpha(c);
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

    std::string string() const {
        return std::string(input_);
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

    tok current_kind() {
        return token_.kind;
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
        using namespace std::string_literals;

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

            // end of file
            case 0:
                character_token(tok::end);
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
                character_token(tok::error);
                return;
            }
        }

        if (!empty()) {
            token_ = {
                loc(), tok::error,
                "Internal lexer error: expected end of input, please open a bug report"s};
            return;
        }
        token_ = {loc(), tok::end, std::string_view(&*stream_, 0)};
        return;
    }

    void character_token(tok kind) {
        if (kind != tok::end) {
            token_ = {loc(), kind, std::string_view(&*stream_, 1)};
        } else {
            token_ = {loc(), kind, std::string_view("end", 3)};
        }
    }

    char character() {
        return *(stream_++);
    }

    token symbol() {
        using namespace std::string_literals;
        const auto start_loc = loc();
        const auto start = stream_;

        while (is_valid_symbol(*stream_)) {
            ++stream_;
        }

        return {start_loc, tok::symbol,
                std::string_view(&(*start), std::distance(start, stream_))};
    }

    token integer() {
        using namespace std::string_literals;
        const auto start_loc = loc();
        const auto start = stream_;

        auto v = *stream_;
        while (v <= '9' && v >= '0') {
            ++stream_;
            v = *stream_;
        }

        return {start_loc, tok::integer,
                std::string_view(&(*start), std::distance(start, stream_))};
    }

    token whitespace() {
        using namespace std::string_literals;
        const auto start_loc = loc();
        const auto start = stream_;

        while (std::iswspace(*stream_)) {
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

tok lexer::current_kind() const {
    return impl_->current_kind();
}

std::string lexer::string() const {
    return impl_->string();
}

lexer::~lexer() = default;

} // namespace lex
