#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <util/envvars.h>
#include <util/expected.h>
#include <util/fs.h>

namespace util {

/// returns the path of the shell currently being used
util::expected<std::filesystem::path, std::string>
current_shell(const envvars::state& envvars) {
    auto env_shell = envvars.get("SHELL");

    if (!env_shell) {
        return util::unexpected("SHELL environment variable is not set");
    }

    const auto raw_path = std::filesystem::path(*env_shell);
    std::error_code ec;
    const auto can_path = std::filesystem::canonical(raw_path, ec);

    if (ec) {
        return util::unexpected(
            fmt::format("unable to determine canonical form of the shell '{}'",
                        raw_path.string()));
    }

    return can_path;
}

int exec(const std::vector<std::string>& args, char* const envp[]) {
    std::vector<char*> argv;

    // clean up temporary files before calling execve, because the descructor
    // that manages their deletion will not be deleted after the current process
    // has been replaced.
    util::clear_temp_dirs();

    // unsafe {
    for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    spdlog::info("exec: {}", fmt::join(args, " "));
    int r;
    if (envp == nullptr) {
        r = execvp(argv[0], argv.data());
    } else {
        r = execvpe(argv[0], argv.data(), envp);
    }
    // } // end unsafe

    spdlog::error("unable to exec");

    return r;
}

} // namespace util
