#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <util/expected.h>
#include <util/fs.h>
#include <util/shell.h>

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

exec_error exec(const std::vector<std::string>& args, char* const envp[]) {
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

    // WARNING: do not log all arguments to the command that is being executbd
    //          because it might leak secrets: it is the responsibility of
    //          callers to log the call appropriately, for example the oras
    //          wrappers that remove secrets before printing.
    int r;
    if (envp == nullptr) {
        spdlog::info("execvp({})", args[0]);
        r = execvp(argv[0], argv.data());
    } else {
        spdlog::info("execvpe({})", args[0]);
        r = execvpe(argv[0], argv.data(), envp);
    }
    // } // end unsafe

    std::string errmsg = fmt::format("unable to exec '{}': {} (errno={})",
                                     args[0], std::strerror(errno), errno);

    spdlog::error("{}", errmsg);

    return exec_error{r, std::move(errmsg)};
}

} // namespace util
