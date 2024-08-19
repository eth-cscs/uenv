#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include <fmt/core.h>

#include <uenv/view.h>
#include <util/expected.h>

namespace uenv {

struct uenv_label {
    std::optional<std::string> name;
    std::optional<std::string> version;
    std::optional<std::string> tag;
    std::optional<std::string> system;
    std::optional<std::string> uarch;
    bool only_name() const {
        return name && !version && !tag && !system && !uarch;
    }
};

bool is_sha(std::string_view v, std::size_t n = 0);

template <unsigned N> struct sha_type {
    std::array<char, N> value;

    std::string string() const {
        return std::string(value.begin(), value.end());
    }

    sha_type() {
        value.fill('0');
    }

    sha_type(const std::string& input) {
        // assert input.size() == N
        // assert input values in correct range a...z,0..9
        if (!is_sha(input, N)) {
            throw std::range_error(
                fmt::format("'{}' is not a valid sha of length {}", input, N));
        }

        std::copy(input.begin(), input.end(), value.begin());
    }

    sha_type& operator=(const sha_type<N>& other) = default;

    friend bool operator==(const sha_type<N>& lhs, const sha_type<N>& rhs) {
        return lhs.value == rhs.value;
    }

    friend bool operator<(const sha_type<N>& lhs, const sha_type<N>& rhs) {
        return lhs.value < rhs.value;
    }
};

using sha256 = sha_type<64>;
using uenv_id = sha_type<16>;

struct uenv_record {
    std::string system;
    std::string uarch;
    std::string name;
    std::string version;
    std::string tag;
    std::string date;
    std::size_t size_byte;
    sha256 sha;
    uenv_id id;
};

bool operator==(const uenv_record& lhs, const uenv_record& rhs);
bool operator<(const uenv_record& lhs, const uenv_record& rhs);

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

struct concrete_uenv {
    /// the name of the uenv
    std::string name;
    /// the path where the uenv will be mounted
    std::filesystem::path mount_path;
    /// the path of the squashfs image to be mounted
    std::filesystem::path sqfs_path;
    /// the path of the meta data - not set if no meta data path was found
    std::optional<std::filesystem::path> meta_path;

    // the fields below are loaded from the meta data in env.json

    /// the (optional) description of the uenv
    std::optional<std::string> description;
    /// the uenv meta data - no set if no meta_path/env.json was found
    std::unordered_map<std::string, concrete_view> views;
};

} // namespace uenv

template <> class fmt::formatter<uenv::uenv_label> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::uenv_label const& d, FmtContext& ctx) const {
        auto ctx_ = ctx.out();
        if (d.name)
            ctx_ = fmt::format_to(ctx.out(), "{}", *d.name);
        else
            ctx_ = fmt::format_to(ctx.out(), "<unamed>");
        if (d.version)
            ctx_ = fmt::format_to(ctx_, "/{}", *d.version);
        if (d.tag)
            ctx_ = fmt::format_to(ctx_, ":{}", *d.tag);
        if (d.system)
            ctx_ = fmt::format_to(ctx_, "@{}", *d.system);
        if (d.uarch)
            ctx_ = fmt::format_to(ctx_, "%{}", *d.uarch);
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

template <> class fmt::formatter<uenv::concrete_uenv> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::concrete_uenv const& e, FmtContext& ctx) const {
        auto ctx_ = fmt::format_to(ctx.out(), "(name='{}', mount={}, sqfs={}",
                                   e.name, e.mount_path, e.sqfs_path);
        if (e.meta_path)
            return fmt::format_to(ctx_, ", meta={})", *e.meta_path);
        return fmt::format_to(ctx_, "meta=none)");
    }
};

template <> class fmt::formatter<uenv::uenv_record> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext>
    constexpr auto format(uenv::uenv_record const& r, FmtContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}/{}:{}@{}%{}", r.name, r.version,
                              r.tag, r.system, r.uarch);
        /*
    std::string system;
    std::string uarch;
    std::string name;
    std::string version;
    std::string tag;
    std::string date;
    std::size_t size_byte;
    std::string sha256;
    std::string id;
        */
    }
};
