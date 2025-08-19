#pragma once

#include <string>
#include <util/expected.h>

namespace uenv {
util::expected<void, std::string> unshare_as_root();

} // namespace uenv
