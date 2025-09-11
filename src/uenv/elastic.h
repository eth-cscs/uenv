#pragma once

#include <string>
#include <vector>

namespace uenv {

void post_elastic(const std::vector<std::string>& payload,
                  const std::string& url, bool subproc);

} // namespace uenv
