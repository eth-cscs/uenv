#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/stat.h>

#include "expected.hpp"

namespace util {

bool is_file(const std::string &fname) {
  struct stat mnt_stat;
  // Check that the input squashfs file exists.
  int sqsh_status = stat(fname.c_str(), &mnt_stat);
  // return false if the path does not exist, or if it exists, it does not refer
  // to a file
  if (sqsh_status || !S_ISREG(mnt_stat.st_mode)) {
    return false;
  }
  return true;
}

util::expected<std::string, std::string> realpath(const std::string &path) {
  char *p = ::realpath(path.c_str(), nullptr);
  if (p) {
    std::string ret(p);
    free(p);
    return ret;
  } else {
    char *err = strerror(errno);
    return util::unexpected(err);
  }
}

bool is_valid_mountpoint(const std::string &mount_point) {
  struct stat mnt_stat;
  auto mnt_status = stat(mount_point.c_str(), &mnt_stat);
  if (mnt_status || !S_ISDIR(mnt_stat.st_mode)) {
    return false;
  }
  return true;
}

} // namespace util
