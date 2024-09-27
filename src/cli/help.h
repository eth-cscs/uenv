#pragma once

#include <memory>
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
        xmpl,
        code,
        info,
        warn,
        depr
    };
    using enum admonition;
    admonition kind = none;
    std::vector<std::string> lines;

    block() = default;
    block(std::string);
    template <typename... Args>
    block(admonition k, Args&&... args)
        : kind(k), lines{std::forward<Args>(args)...} {
    }
};
std::string render(const block&);

struct linebreak {};
std::string render(const linebreak&);

// type erasure for items to print in a help message.
// any type T for which the following is provided will be supported
//   std::string render(const T&)
template <typename T>
concept Renderable = requires(T v) {
    { render(v) } -> std::convertible_to<std::string>;
};

class item {
  public:
    template <Renderable T>
    item(T impl) : impl_(std::make_unique<wrap<T>>(std::move(impl))) {
    }

    item(item&& other) = default;

    item(const item& other) : impl_(other.impl_->clone()) {
    }

    item& operator=(item&& other) = default;
    item& operator=(const item& other) {
        return *this = item(other);
    }

    std::string render() const {
        return impl_->render();
    }

  private:
    struct interface {
        virtual ~interface() {
        }
        virtual std::unique_ptr<interface> clone() = 0;
        virtual std::string render() const = 0;
    };

    std::unique_ptr<interface> impl_;

    template <Renderable T> struct wrap : interface {
        explicit wrap(const T& impl) : wrapped(impl) {
        }
        explicit wrap(T&& impl) : wrapped(std::move(impl)) {
        }

        virtual std::unique_ptr<interface> clone() override {
            return std::unique_ptr<interface>(new wrap<T>(wrapped));
        }

        virtual std::string render() const override {
            return help::render(wrapped);
        }

        T wrapped;
    };
};

} // namespace help

template <> class fmt::formatter<help::item> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(help::item const& item, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", item.render());
    }
};

template <> class fmt::formatter<help::lst> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(help::lst const& l, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", ::color::yellow(l.content));
    }
};
