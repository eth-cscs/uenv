#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <util/cgroup.h>

namespace util {

std::optional<std::string> current_cgroup() {
    std::ifstream in{"/proc/self/cgroup"};
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    auto contents = buffer.str();
    const auto end = contents.find_last_not_of(" \t\n\r");
    if (end == std::string::npos) {
        return {};
    }
    contents.resize(end + 1);
    return contents;
}

bool cgroup_is_slurm_managed(std::string_view cgroup) {
    // the job-id component is present in both the v1 and v2 layouts. The
    // leading '/' keeps an unrelated component that merely ends in "job_"
    // from matching.
    return cgroup.find("/job_") != std::string_view::npos;
}

} // namespace util
