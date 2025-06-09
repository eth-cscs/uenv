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

util::expected<uenv::repository, std::string>
registry_listing(const std::string& nspace);

std::string registry_url();

util::expected<std::optional<uenv::oras::credentials>, std::string>
get_credentials(std::optional<std::string> username,
                std::optional<std::string> token);

// Create site-specific registry backend (JFrog implementation)
std::unique_ptr<uenv::registry_backend> create_site_registry();

} // namespace site
