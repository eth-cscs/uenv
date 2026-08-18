#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

#include <util/expected.h>

namespace util {

// Enter the namespace ns (e.g. "user", "mnt") of the process pid, via
// setns(2).
util::expected<void, std::string> namespace_join(pid_t pid,
                                                 const std::string& ns);

// Enter each of ns_names of the process pid, in the order given.
util::expected<void, std::string>
namespaces_join(pid_t pid, const std::vector<std::string>& ns_names);

} // namespace util
