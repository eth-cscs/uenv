#pragma once

#include <filesystem>

#include <util/envvars.h>
#include <util/expected.h>
#include <util/lustre.h>

namespace lustre {

struct status {
    // number of OSTs to stripe over (-1 -> all)
    // >=1 when the output of getstripe
    // can equal -1 when used as an input to setstripe
    std::int64_t count = -1;

    // number of bytes per stripe
    std::uint64_t size = 1024u * 1024u;

    // OST index of first stripe (-1 -> default)
    // >=0 when the output of getstripe on a file
    // may be -1 as ain input, or the result of getstripe on a directory
    std::int64_t index = -1;
};

// manages the configuration
struct lpath {
    status config;
    std::filesystem::path path;
    std::filesystem::path lfs;

    bool is_regular_file() const {
        return std::filesystem::is_regular_file(path);
    }
    bool is_directory() const {
        return std::filesystem::is_directory(path);
    }
};

// types of error that can be generated when making lustre calls
enum class error {
    okay,       // what it says on the tin
    not_lustre, // not a lustre file system
    no_lfs,     // the lfs utility was not available on the system
    lfs,        // there was an error calling lfs
    other,      // ...
};

util::expected<lpath, error> loadpath(const std::filesystem::path& p,
                                      const envvars::state& env);
bool is_striped(const lpath& p);

// return true if p is a regular file or directory in a lustre filesystem
// return false under all other circumstances
bool is_lustre(const std::filesystem::path& p);

// get striping information about a file or path
util::expected<status, error> getstripe(const std::filesystem::path& p,
                                        const envvars::state& env);

bool setstripe(const std::filesystem::path& p, const status&,
               const envvars::state& env);

} // namespace lustre

template <> class fmt::formatter<lustre::status> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(lustre::status const& s, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "(count={}, size={}, index={})",
                              s.count, s.size, s.index);
    }
};

template <> class fmt::formatter<lustre::error> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(lustre::error const& e, FmtContext& ctx) const {
        using enum lustre::error;
        switch (e) {
        case okay:
            return format_to(ctx.out(), "unset");
        case not_lustre:
            return format_to(ctx.out(), "not a lustre filesystem");
        case no_lfs:
            return format_to(ctx.out(), "lfs is not available");
        case lfs:
            return format_to(ctx.out(), "internal lfs error");
        case other:
            return format_to(ctx.out(), "unknonwn");
        }
        return format_to(ctx.out(), "unknonwn");
    }
};
