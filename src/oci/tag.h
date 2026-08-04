#pragma once

#include <string>
#include <string_view>

#include <util/expected.h>
#include <util/parse.h>

namespace oci {

class tag;

// An OCI manifest tag: a mutable, human-readable reference matching the OCI tag
// grammar ([a-zA-Z0-9_][a-zA-Z0-9._-]{0,127}). A `tag` is always syntactically
// valid — it can only be obtained by parsing (which validates the grammar) — so
// constructing one never fails, and callers never juggle an unvalidated string
// against a real tag or a digest.
class tag {
  public:
    // parse text against the OCI tag grammar. rejects a leading '.' or '-', an
    // empty value, an out-of-grammar character, and a value longer than 128
    // characters. a thin forwarder to oci::parse_tag.
    static util::expected<tag, util::parse_error> parse(std::string_view text);

    // the tag text.
    const std::string& string() const;

    friend bool operator==(const tag&, const tag&) = default;
    friend util::expected<tag, util::parse_error> parse_tag(std::string_view);

  private:
    explicit tag(std::string value) : value_(std::move(value)) {
    }
    std::string value_;
};

} // namespace oci

#include <fmt/core.h>
template <> class fmt::formatter<oci::tag> {
  public:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    template <typename FmtContext>
    auto format(oci::tag const& t, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", t.string());
    }
};
