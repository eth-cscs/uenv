#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <uenv/repository.h>
#include <util/expected.h>
#include <util/url.h>

namespace site {

// the CSCS uenv listing service: the base URL used when registry.listing_url is
// not set in the configuration. Also printed by `uenv config`, which is why it
// is here rather than a literal inside registry_listing.
inline constexpr std::string_view default_listing_url =
    "https://uenv-list.svc.cscs.ch/list";

// parse the JSON document returned by the listing service into the records
// that belong to `nspace`. Records that fail to parse are dropped with a
// warning; a document that is not the expected shape is an error.
util::expected<std::vector<uenv::uenv_record>, std::string>
parse_registry_listing(std::string_view body, const std::string& nspace);

// query the uenv listing service for the uenv available in a namespace.
// listing_url overrides the service base URL (default: default_listing_url
// above) — used to point at a local/mock endpoint for testing.
util::expected<uenv::repository, std::string>
registry_listing(const std::optional<util::url>& listing_url,
                 const std::string& nspace);

// return the name of the current system from the calling environment.
// on CSCS systems this is derived from the CLUSTER_NAME environment variable.
// TODO: remove once setting system name has been tested using system config
// files.
std::optional<std::string> get_system_name(const envvars::state&);

} // namespace site
