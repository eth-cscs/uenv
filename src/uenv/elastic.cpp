#include <unistd.h>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <uenv/elastic.h>
#include <util/curl.h>
#include <util/detach.h>

namespace uenv {

void post_elastic(const std::vector<std::string>& payload,
                  const std::string& url, bool subproc) {
    if (subproc) {
        // create a sub-process to asynchronously post the results
        if (fork() == 0) {
            // keep stdout/stderr if trace logging is enabled, so that it is
            // still possble to get trace curl output.
            const bool drop_out = spdlog::get_level() > spdlog::level::debug;

            // turn off logging
            if (drop_out) {
                spdlog::set_level(spdlog::level::off);
                spdlog::set_error_handler([](const std::string&) {});
            }

            // do not use stderr/stdin/stdout from parent process because
            // this does not play nicely with Slurm, particularly with the
            // --pty flag and srun.
            // input is always disabled
            if (drop_out) {
                (void)util::redirect_to_null(
                    {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO});
            } else {
                (void)util::redirect_to_null({STDIN_FILENO});
            }

            // send the telemetry payload to elastic
            for (auto& text : payload) {
                // use 10s timeout
                if (auto result =
                        util::curl::post(text, url, "application/json", 10000);
                    !result) {
                    spdlog::warn("post_elastic: {}", result.error().message);
                    break;
                }
                spdlog::debug("post_elastic telemetry asynchronously to {}: {}",
                              url, text);
            }

            // safer than exit()
            _exit(0);
        }
        spdlog::debug("post_elastic: posting logs asynchronously");
    } else {
        for (auto& text : payload) {
            // use 5s timeout
            if (auto result =
                    util::curl::post(text, url, "application/json", 5000);
                !result) {
                spdlog::debug("unable to log to elastic: {}",
                              result.error().message);
                break;
            }
            spdlog::debug("posted elastic telemetry {}", text);
        }
    }
}

} // namespace uenv
