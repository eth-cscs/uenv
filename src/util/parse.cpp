#include <algorithm>
#include <string>

#include <fmt/format.h>

#include <util/parse.h>

namespace util {

std::string parse_error::message() const {
    return fmt::format("{}\n  {}\n  {}{}", detail, input, std::string(loc, ' '),
                       std::string(std::max(1u, width), '^'));
}

util::expected<void, parse_error> expect_end(lex::lexer& L,
                                             std::string_view what) {
    if (L != lex::tok::end) {
        const auto t = L.peek();
        return util::unexpected(parse_error{
            L.string(), fmt::format("unexpected '{}' in {}", t.spelling, what),
            t});
    }
    return {};
}

} // namespace util
