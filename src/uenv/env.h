#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

#include <fmt/core.h>

#include <util/expected.h>

namespace uenv {

struct view_description {
    std::optional<std::string> uenv;
    std::string name;
};

struct uenv_label {
    std::string name;
    std::optional<std::string> version;
    std::optional<std::string> tag;
};

struct uenv_description {
    uenv_description(uenv_label label);
    uenv_description(uenv_label label, std::string mount);
    uenv_description(std::string file);
    uenv_description(std::string file, std::string mount);
    uenv_description() = default;

    std::optional<uenv_label> label() const;
    std::optional<std::string> filename() const;
    std::optional<std::string> mount() const;

  private:
    std::variant<uenv_label, std::string> value_;
    std::optional<std::string> mount_;
};

struct uenv_concretisation {
    std::string name;
    std::filesystem::path mount;
    std::filesystem::path sqfs;
    std::optional<std::filesystem::path> meta_path;
};

struct parse_error {
    std::string msg;
    unsigned loc;
    parse_error(std::string msg, unsigned loc) : msg(std::move(msg)), loc(loc) {
    }
};

} // namespace uenv

template <> class fmt::formatter<uenv::view_description> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::view_description const& d,
                          FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "(name={}, uenv={})", d.name,
                              (d.uenv ? *d.uenv : "none"));
    }
};

template <> class fmt::formatter<uenv::uenv_label> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::uenv_label const& d, FmtContext& ctx) const {
        auto ctx_ = fmt::format_to(ctx.out(), "{}", d.name);
        if (d.version)
            ctx_ = fmt::format_to(ctx_, "/{}", *d.version);
        if (d.tag)
            ctx_ = fmt::format_to(ctx_, ":{}", *d.tag);
        return ctx_;
    }
};

template <> class fmt::formatter<uenv::uenv_description> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::uenv_description const& d,
                          FmtContext& ctx) const {
        auto ctx_ = fmt::format_to(ctx.out(), "(");
        if (const auto fname = d.filename())
            ctx_ = fmt::format_to(ctx_, "file={}, ", *fname);
        if (const auto label = d.label())
            ctx_ = fmt::format_to(ctx_, "label={}, ", *label);
        if (const auto mount = d.mount())
            return fmt::format_to(ctx_, "mount={})", *mount);
        return fmt::format_to(ctx_, "mount=none)");
    }
};

template <> class fmt::formatter<uenv::uenv_concretisation> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::uenv_concretisation const& e,
                          FmtContext& ctx) const {
        auto ctx_ = fmt::format_to(ctx.out(), "(name='{}', mount={}, sqfs={}",
                                   e.name, e.mount, e.sqfs);
        if (e.meta_path)
            return fmt::format_to(ctx_, ", meta={})", *e.meta_path);
        return fmt::format_to(ctx_, "meta=none)");
    }
};
