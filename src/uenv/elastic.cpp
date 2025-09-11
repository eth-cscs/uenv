#include <fcntl.h>
#include <unistd.h>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <uenv/elastic.h>
#include <util/curl.h>

namespace uenv {

void post_elastic(const std::vector<std::string>& payload,
                  const std::string& url, bool subproc) {
    if (subproc) {
        // create a sub-process to asynchronously post the results
        if (fork() == 0) {
            // turn off logging
            spdlog::set_level(spdlog::level::off);
            spdlog::set_error_handler([](const std::string&) {});

            // do not use stderr/stdin/stdout from parent process because
            // this does not play nicely with Slurm, particularly with the
            // --pty flag and srun.
            int null_fd = open("/dev/null", O_RDWR);
            if (null_fd != -1) {
                dup2(null_fd, STDIN_FILENO);
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);

                if (null_fd > 2) {
                    // close extra fd if not 0,1,2
                    close(null_fd);
                }
            }

            // send the telemetry payload to elastic
            for (auto& text : payload) {
                if (!util::curl::post(text, url)) {
                    break;
                }
            }

            // safer than exit()
            _exit(0);
        }
        spdlog::debug("posted elastic telemetry asynchronously {}",
                      fmt::join(payload, ","));
    } else {
        for (auto& text : payload) {
            if (auto result = util::curl::post(text, url); !result) {
                spdlog::debug("unable to log to elastic: {}",
                              result.error().message);
                continue;
            }
            spdlog::debug("posted elastic telemetry {}", text);
        }
    }
}

} // namespace uenv
