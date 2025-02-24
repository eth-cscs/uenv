#include <uenv/parse.h>

#include "config.h"

namespace uenv {

config_base config_merge(const config_base& lhs, const config_base& rhs) {
    return {
        .repo = lhs.repo   ? lhs.repo
                : rhs.repo ? rhs.repo
                           : std::nullopt,
        .color = lhs.color   ? lhs.color
                 : rhs.color ? rhs.color
                             : std::nullopt,
    };
}

config_base default_config() {

    return {};
}

config::config(const config_base& base) {
    if (base.repo) {
        auto path = parse_path(base.repo.value());
    } else {
    }
}

} // namespace uenv
