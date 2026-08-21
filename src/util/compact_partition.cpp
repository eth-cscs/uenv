#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

#include <util/compact_partition.h>
#include <util/lex.h>
#include <util/strings.h>

namespace util {

namespace {

// a compact_partition section count/size is always non-negative, so only
// unsigned values are accepted here.
util::expected<unsigned, parse_error> parse_uint(lex::lexer& L) {
    const auto t = L.peek();
    if (t != lex::tok::integer) {
        return util::unexpected(parse_error{
            L.string(),
            fmt::format("expected an integer, found '{}'", t.spelling), t});
    }

    unsigned value;
    const auto result = std::from_chars(
        t.spelling.data(), t.spelling.data() + t.spelling.size(), value);
    if (result.ec != std::errc{}) {
        return util::unexpected(parse_error{
            L.string(),
            fmt::format("'{}' is not a valid unsigned integer", t.spelling),
            t});
    }

    L.next();
    return value;
}

// section := SIZE | SIZE '(' 'x' COUNT ')'
util::expected<compact_partition::section, parse_error>
parse_section(lex::lexer& L) {
    compact_partition::section result{.count = 1u, .size = 0u};

    PARSE(L, uint, result.size);

    // a bare SIZE with no '(x COUNT)' suffix is a section with count 1
    if (L.current_kind() != lex::tok::lparen) {
        return result;
    }
    L.next(); // eat '('

    const auto xt = L.peek();
    if (xt != lex::tok::symbol || xt.spelling != "x") {
        return util::unexpected(parse_error{
            L.string(),
            fmt::format("expected 'x' after '(', found '{}'", xt.spelling),
            xt});
    }
    L.next(); // eat 'x'

    PARSE(L, uint, result.count);

    if (L.current_kind() != lex::tok::rparen) {
        const auto t = L.peek();
        return util::unexpected(
            parse_error{L.string(),
                        fmt::format("expected ')' to close section, found '{}'",
                                    t.spelling),
                        t});
    }
    L.next(); // eat ')'

    return result;
}

// partition := section (',' section)*
util::expected<compact_partition, parse_error>
parse_compact_partition(lex::lexer& L) {
    compact_partition result;

    while (true) {
        compact_partition::section s;
        PARSE(L, section, s);
        result.add_section(s.count, s.size);

        if (L.current_kind() != lex::tok::comma) {
            break;
        }
        L.next(); // eat ','

        // handle trailing comma elegantly
        if (L.peek() == lex::tok::end) {
            break;
        }
    }

    return result;
}

} // namespace

util::expected<compact_partition, parse_error>
parse_compact_partition(std::string_view text) {
    const std::string sanitised = util::strip(text);
    auto L = lex::lexer(sanitised);

    auto result = parse_compact_partition(L);
    if (!result) {
        return result;
    }

    if (auto e = util::expect_end(L, "compact partition"); !e) {
        return util::unexpected(e.error());
    }

    return result;
}

void compact_partition::add_section(unsigned count, unsigned size) {
    sections_.push_back({.count = count, .size = size});
    size_ += count * size;
    num_slots_ += count;
}

std::optional<unsigned> compact_partition::find_slot(unsigned rank) const {
    // requested rank is out of bounds?
    if (rank >= size_) {
        return {};
    }

    unsigned offset = 0u; // rank of the first task in the current section
    unsigned slot = 0u;   // slot of the first task in the current section
    for (auto s : sections_) {
        const auto local_size = s.size * s.count;
        if (offset + local_size <= rank) {
            offset += local_size;
            slot += s.count;
            continue;
        }
        return slot + (rank - offset) / s.size;
    }

    // we shouldn't ever run off the end
    return {};
}

std::optional<unsigned> compact_partition::find_size(unsigned slot) const {
    // check for out of bounds
    if (slot >= num_slots_) {
        return {};
    }

    unsigned pos = 0u;
    for (auto& [count, size] : sections_) {
        if (slot < pos + count) {
            return size;
        }
        pos += count;
    }

    // this should never be reached
    return {};
}

const std::vector<compact_partition::section>&
compact_partition::sections() const {
    return sections_;
}

} // namespace util
