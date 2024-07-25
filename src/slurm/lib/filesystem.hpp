#include <string>

#include "expected.hpp"

namespace util {

// return true if fname exists and is a file
bool is_file(const std::string &fname);
bool is_valid_mountpoint(const std::string &mount_point);

util::expected<std::string, std::string> realpath(const std::string &path);

} // namespace util
