// vim: ts=4 sts=4 sw=4 et

#include <vector>

#include <fmt/core.h>
#include <tinyopt/tinyopt.h>

namespace uenv {

void start_help();

class start_info {
    std::string img_argument;

  public:
    std::vector<to::option> options();
};

} // namespace uenv
