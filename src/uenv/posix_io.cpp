#include "util/expected.h"
#include <cerrno>
#include <fcntl.h>
#include <fmt/format.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>

namespace uenv {

util::expected<int, std::string> openat(int dirfd, std::filesystem::path file,
                                        int oflag) {
    int fd = ::openat(dirfd, file.c_str(), oflag);
    if (fd < 0) {
        return util::unexpected(fmt::format("opening {} failed with {}",
                                            file.string(), strerror(errno)));
    }
    return fd;
}

util::expected<void, std::string> write(int fd, const void* buf, size_t count) {
    ssize_t n = ::write(fd, buf, count);
    if (n < 0) {
        return util::unexpected(fmt::format("write({}, {}, {}) failed with {}",
                                            fd, (char*)buf, count,
                                            strerror(errno)));
    }
    if (static_cast<size_t>(n) != count) {
        return util::unexpected(
            fmt::format("short write: wrote {} of {} bytes", n, count));
    }
    return {};
}

} // namespace uenv
