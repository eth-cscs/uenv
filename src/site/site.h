#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
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

// the namespaces of the CSCS registry: tab completion offers them whether or
// not a listing of them has been cached.
inline constexpr std::string_view registry_namespaces[] = {"build", "deploy",
                                                           "service"};

// query the uenv listing service for the uenv available in a namespace.
// listing_url overrides the service base URL (default: default_listing_url
// above) — used to point at a local/mock endpoint for testing.
// If cache_root is set, the listing is saved in the cache (see below).
util::expected<uenv::repository, std::string>
registry_listing(const std::optional<util::url>& listing_url,
                 const std::string& nspace,
                 const std::optional<std::filesystem::path>& cache_root);

// Every listing that registry_listing fetches is saved, so that tab completion
// can offer the labels in a registry without a network request. The cache of
// a listing service is a directory under cache_root (uenv::user_cache_path),
// named after a hash of its URL, with one file per namespace holding the
// document returned by the service.
std::filesystem::path
listing_cache_dir(const std::filesystem::path& cache_root,
                  const std::optional<util::url>& listing_url);

// a cached listing older than this is not used
inline constexpr auto listing_cache_max_age = std::chrono::days(30);

// a cached listing larger than this is not used
inline constexpr std::uintmax_t listing_cache_max_size = 16 * 1024 * 1024;

// tab completion refreshes a cached listing that is older than this
inline constexpr auto listing_refresh_interval = std::chrono::seconds(60);

// save the document `body` returned by the listing service for `nspace` in the
// cache directory `dir`. The file is replaced atomically, so a reader never
// sees it half written. Failure is not an error: the cache is only used for
// tab completion.
void save_registry_listing(const std::filesystem::path& dir,
                           const std::string& nspace, std::string_view body);

// the records of `nspace` in the cache directory `dir`, or nullopt if there
// is no listing of it, or it is older than listing_cache_max_age or invalid.
std::optional<std::vector<uenv::uenv_record>>
cached_registry_listing(const std::filesystem::path& dir,
                        const std::string& nspace);

// the namespaces that have a listing in the cache directory `dir`
std::vector<std::string> cached_namespaces(const std::filesystem::path& dir);

// Whether the caller should refresh the listing of `nspace` in the cache
// directory `dir`: it is due if it is older than listing_refresh_interval, or
// if `usable` is false (it is missing or invalid). A refresh is claimed by
// touching a stamp file, .<nspace>.refresh, and is not claimed again until the
// stamp is listing_refresh_interval old, whether or not the refresh succeeded:
// that limits the requests to the listing service to one per namespace per
// interval, including while it is unreachable. Returns false if the stamp
// can't be written. There is no lock: two callers can both claim a refresh.
bool claim_listing_refresh(const std::filesystem::path& dir,
                           const std::string& nspace, bool usable);

// fetch the listing of `nspace` into the cache (see registry_listing), and
// remove the temporary files left in the cache directory by writers that were
// killed.
void refresh_registry_listing(const std::optional<util::url>& listing_url,
                              const std::string& nspace,
                              const std::filesystem::path& cache_root);

// return the name of the current system from the calling environment.
// on CSCS systems this is derived from the CLUSTER_NAME environment variable.
// TODO: remove once setting system name has been tested using system config
// files.
std::optional<std::string> get_system_name(const envvars::state&);

} // namespace site
