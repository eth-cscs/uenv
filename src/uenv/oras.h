#pragma once

#include <string>
#include <vector>

#include <uenv/uenv.h>

namespace uenv {
namespace oras {

bool pull(std::string rego, std::string nspace);

util::expected<std::vector<std::string>, std::string>
discover(const std::string& registry, const std::string& nspace,
         const uenv_record& uenv,
         const std::optional<std::string> token=std::nullopt);

util::expected<void, int> pull_digest(const std::string& registry,
                                      const std::string& nspace,
                                      const uenv_record& uenv,
                                      const std::string& digest,
                                      const std::filesystem::path& destination,
                                      const std::optional<std::string> token=std::nullopt);

util::expected<void, int> pull_tag(const std::string& registry,
                                   const std::string& nspace,
                                   const uenv_record& uenv,
                                   const std::filesystem::path& destination
                                   const std::optional<std::string> token=std::nullopt);

} // namespace oras
} // namespace uenv
