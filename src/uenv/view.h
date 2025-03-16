#pragma once

#include <optional>
#include <string>

#include <util/envvars.h>

namespace uenv {

/// The result of parsing a uenv description from the command line.
struct view_description {
    /// name of the uenv that provides the view -
    /// optional because view descriptions are in one of two forms:
    /// - view_name
    /// - uenv_name:view_name
    std::optional<std::string> uenv;
    /// name of the view
    std::string name;
};

/// A fully qualified uenv view
struct qualified_view_description {
    /// name of the uenv that provides the view
    std::string uenv;
    /// name of the view
    std::string name;
};

struct concrete_view {
    /// the name of the view
    std::string name;
    /// the string description of the view: loaded from env.json meta data
    std::string description;
    /// the environment variable updates to be applied when the view is loaded:
    /// loaded from env.json meta data
    envvars::patch environment;
};

} // namespace uenv

#include <fmt/core.h>
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
