#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <fmt/core.h>

namespace util {

// https://semver.org/
struct semver {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    std::optional<std::string> prerelease = std::nullopt;
    std::optional<std::string> build = std::nullopt;
};

bool operator<(const semver&, const semver&);
bool operator==(const semver&, const semver&);

} // namespace util

template <> class fmt::formatter<util::semver> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(util::semver const& v, FmtContext& ctx) const {
        auto out =
            fmt::format_to(ctx.out(), "{}.{}.{}", v.major, v.minor, v.patch);
        if (v.prerelease) {
            out = fmt::format_to(out, "-{}", v.prerelease.value());
        }
        if (v.build) {
            out = fmt::format_to(out, "+{}", v.build.value());
        }
        return out;
    }
};
