#pragma once

#include <string>
#include <unordered_map>

#include <uenv/env.h>
#include <uenv/envvars.h>

// common code for working with environment variables used by different CLI
// modes

namespace uenv {

std::unordered_map<std::string, std::string> getenv(const env&);

util::expected<int, std::string>
setenv(const std::unordered_map<std::string, std::string>& variables);

} // namespace uenv
