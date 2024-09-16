#pragma once

#include <string>
#include <vector>

#include <fmt/color.h>
#include <fmt/format.h>

#include "color.h"

namespace help {

struct lst {
    std::string content;
};

struct block {
    enum class admonition : std::uint32_t {
        none,
        note,
        info,
        warn,
        deprecated
    };
    using enum admonition;
    admonition kind = none;
    std::vector<std::string> lines;
};

struct example {
    std::vector<std::string> description;
    std::vector<std::string> code;
    std::vector<block> blocks;
};

} // namespace help

template <> class fmt::formatter<help::lst> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(help::lst const& listing, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", ::color::white(listing.content));
    }
};

template <> class fmt::formatter<help::block> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(help::block const& block, FmtContext& ctx) const {
        using enum help::block::admonition;
        auto ctx_ = ctx.out();
        switch (block.kind) {
        case none:
            break;
        case note:
            ctx_ = fmt::format_to(ctx.out(), "{} - ", ::color::cyan("note"));
            break;
        case info:
            ctx_ = fmt::format_to(ctx.out(), "{} - ",
                                  ::color::bright_green("info"));
            break;
        case warn:
            ctx_ = fmt::format_to(ctx.out(), "{} - ",
                                  ::color::bright_red("warning"));
            break;
        case deprecated:
            ctx_ = fmt::format_to(ctx.out(), "{} - ",
                                  ::color::bright_red("deprecated"));
        }
        bool first = true;
        for (auto& l : block.lines) {
            if (!first) {
                ctx_ = fmt::format_to(ctx_, "\n");
            }
            ctx_ = fmt::format_to(ctx_, "{}", l);
            first = false;
        }

        return ctx_;
    }
};

template <typename FmtContext>
constexpr auto format_lines(FmtContext& ctx,
                            const std::vector<std::string>& lines,
                            std::string_view prefix = "") {
    auto ctx_ = ctx;
    bool first = true;
    for (auto& l : lines) {
        if (!first) {
            ctx_ = fmt::format_to(ctx_, "\n");
        }
        ctx_ = fmt::format_to(ctx_, "{}{}", prefix, l);
    }

    return ctx_;
}

template <> class fmt::formatter<help::example> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(help::example const& E, FmtContext& ctx) const {
        using namespace color;
        auto ctx_ = fmt::format_to(ctx.out(), "{} - ", blue("Example"));
        ctx_ = format_lines(ctx_, E.description);
        ctx_ = fmt::format_to(ctx_, ":");
        for (auto& l : E.code) {
            ctx_ = fmt::format_to(ctx_, "\n  {}", white(l));
        }
        if (!E.blocks.empty()) {
            ctx_ = fmt::format_to(ctx_, "\n");
        }
        for (auto& b : E.blocks) {
            ctx_ = fmt::format_to(ctx_, "{}", b);
        }

        return ctx_;
    }
};
