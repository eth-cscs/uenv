#pragma once

#include <string>

#include <uenv/repository.h>
#include <util/expected.h>

namespace site {

util::expected<uenv::repository, std::string>
registry_listing(const std::string& nspace);

// return the name of the current system from the calling environment.
// on CSCS systems this is derived from the CLUSTER_NAME environment variable.
// TODO: remove once setting system name has been tested using system config
// files.
std::optional<std::string> get_system_name(const envvars::state&);

} // namespace site
