#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>

#include <uenv/uenv.h>

namespace uenv {
namespace oras {

struct credentials {
    std::string username;
    std::string token;
};

bool pull(std::string rego, std::string nspace);

util::expected<std::vector<std::string>, std::string>
discover(const std::string& registry, const std::string& nspace,
         const uenv_record& uenv,
         const std::optional<credentials> token = std::nullopt);

util::expected<void, int>
pull_digest(const std::string& registry, const std::string& nspace,
            const uenv_record& uenv, const std::string& digest,
            const std::filesystem::path& destination,
            const std::optional<credentials> token = std::nullopt);

util::expected<void, int>
pull_tag(const std::string& registry, const std::string& nspace,
         const uenv_record& uenv, const std::filesystem::path& destination,
         const std::optional<credentials> token = std::nullopt);

} // namespace oras
} // namespace uenv

template <> class fmt::formatter<uenv::oras::credentials> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::oras::credentials const& c,
                          FmtContext& ctx) const {
        // replace token characters with 'X'
        return fmt::format_to(ctx.out(), "{{username: {}, token: {:X>{}}}}",
                              c.username, "", c.token.size());
    }
};
