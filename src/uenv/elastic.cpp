#include <fcntl.h>
#include <unistd.h>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <uenv/elastic.h>
#include <util/curl.h>

namespace uenv {

namespace impl {

void drop_file_descriptors(bool in, bool out) {
    if (in || out) {
        int null_fd = open("/dev/null", O_RDWR);

        if (null_fd != -1) {
            if (in) {
                dup2(null_fd, STDIN_FILENO);
            }
            if (out) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
            }

            if (null_fd > 2) {
                close(null_fd);
            }
        }
    }
}

} // namespace impl

void post_elastic(const std::vector<std::string>& payload,
                  const std::string& url, bool subproc) {
    if (subproc) {
        // create a sub-process to asynchronously post the results
        if (fork() == 0) {
            // always disable input
            const bool drop_in = true;
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
            impl::drop_file_descriptors(drop_in, drop_out);

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
