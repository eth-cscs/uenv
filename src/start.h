// vim: ts=4 sts=4 sw=4 et

#include <vector>

#include <fmt/core.h>

#include "uenv.h"

namespace uenv {

void start_help();

struct start_options {
    std::string image;
    // std::vector<to::option> cli_options(int &mode, std::string &name);
};

void start(const start_options &options, const global_options &settings);

} // namespace uenv

template <> class fmt::formatter<uenv::start_options> {
  public:
    // parse format specification and store it:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.end();
    }
    // format a value using stored specification:
    template <typename FmtContext> constexpr auto format(uenv::start_options const &opts, FmtContext &ctx) const {
        return fmt::format_to(ctx.out(), "start_options(image {})", opts.image);
    }
};
