#include <cstring>
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/setns.h>

namespace util {

// Join a specific namespace.
util::expected<void, std::string> namespace_join(pid_t pid,
                                                 const std::string& ns) {
    std::string path;
    int fd;

    path = fmt::format("/proc/{}/ns/{}", pid, ns);
    fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) {
            return util::unexpected(
                fmt::format("join: no PID {}: {} not found", pid, path));
        } else {
            return util::unexpected(fmt::format("join: can't open {}", path));
        }
    }
    // setns(2) seems to be involved in some kind of race with syslog(3).
    // Rarely, when configured with --enable-syslog, the call fails with
    // EINVAL. We never figured out a proper fix, so just retry a few times in
    // a loop. See issue https://github.com/hpc/charliecloud/issues/1270.
    for (int i = 1; setns(fd, 0) != 0; i++) {
        if (i >= 5) {
            return util::unexpected(
                fmt::format("can’t join {} namespace of pid {}", ns, pid));
        } else {
            spdlog::warn("can’t join {} namespace; trying again, {}", ns,
                         strerror(errno));
            sleep(1);
        }
    }
    return {};
}

// Join the existing namespaces containing process pid.
util::expected<void, std::string>
namespaces_join(pid_t pid, const std::vector<std::string>& ns_names) {
    for (auto& ns : ns_names) {
        spdlog::trace("joining namespace {} of pid {}", ns, pid);
        auto r = namespace_join(pid, ns);
        if (!r) {
            return r;
        }
    }
    return {};
}

} // namespace util
