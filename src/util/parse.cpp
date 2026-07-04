#include <algorithm>
#include <string>

#include <fmt/format.h>

#include <util/parse.h>

namespace util {

std::string parse_error::message() const {
    return fmt::format("{}\n  {}\n  {}{}", detail, input, std::string(loc, ' '),
                       std::string(std::max(1u, width), '^'));
}

} // namespace util
