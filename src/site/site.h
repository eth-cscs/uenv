#pragma once

#include <memory>
#include <optional>
#include <string>

#include <uenv/oras.h>
#include <uenv/registry.h>
#include <uenv/repository.h>
#include <util/envvars.h>
#include <util/expected.h>

namespace site {

// return the name of the current system
// on CSCS systems this is derived from the CLUSTER_NAME environment variable
std::optional<std::string> get_system_name(const std::optional<std::string>,
                                           const envvars::state&);

std::optional<std::string> get_username();

// default namespace for image deployment
std::string default_namespace();

util::expected<std::optional<uenv::oras::credentials>, std::string>
get_credentials(std::optional<std::string> username,
                std::optional<std::string> token);

// Create site-specific registry (JFrog implementation)
uenv::registry create_site_registry();

} // namespace site
