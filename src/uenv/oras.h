#pragma once

#include <string>
#include <vector>

#include <uenv/uenv.h>

namespace uenv {
namespace oras {

bool pull(std::string rego, std::string nspace);

util::expected<std::vector<std::string>, std::string>
discover(const std::string& registry, const std::string& nspace,
         const uenv_record& uenv);

} // namespace oras
} // namespace uenv
