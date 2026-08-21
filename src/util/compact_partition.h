#include <optional>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <util/expected.h>
#include <util/parse.h>

namespace util {
class compact_partition {
  public:
    struct section {
        unsigned count;
        unsigned size;
    };

    compact_partition() = default;

    void add_section(unsigned count, unsigned size);
    std::optional<unsigned> find_slot(unsigned rank) const;
    std::optional<unsigned> find_size(unsigned slot) const;

    const std::vector<section>& sections() const;

  private:
    std::vector<section> sections_;
    unsigned size_ = 0u;
    unsigned num_slots_ = 0u;
};

// parse a compact_partition from its compact string representation, e.g.
// "5,4(x3)" -> sections [{count:1,size:5}, {count:3,size:4}]
//
// grammar:
//   partition := section (',' section)*
//   section   := SIZE | SIZE '(' 'x' COUNT ')'
//
// this is the grammar used by SLURM_STEP_TASKS_PER_NODE and matches the
// output of the fmt::formatter below, so parsing and formatting round-trip.
util::expected<compact_partition, parse_error>
parse_compact_partition(std::string_view text);
} // namespace util

template <> class fmt::formatter<util::compact_partition::section> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(util::compact_partition::section const& s,
                          FmtContext& ctx) const {
        if (s.count == 1u) {
            return fmt::format_to(ctx.out(), "{}", s.size);
        }
        return fmt::format_to(ctx.out(), "{}(x{})", s.size, s.count);
    }
};

template <> class fmt::formatter<util::compact_partition> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(util::compact_partition const& p,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", fmt::join(p.sections(), ","));
    }
};
