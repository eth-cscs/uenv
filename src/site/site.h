#pragma once

#include <string>

#include <uenv/repository.h>
#include <util/expected.h>

namespace site {

util::expected<uenv::repository, std::string>
registry_listing(const std::string& nspace);

} // namespace site
