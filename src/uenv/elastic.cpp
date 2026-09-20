#include <chrono>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <uenv/elastic.h>
#include <util/curl.h>
#include <util/detach.h>

namespace uenv {

// the time the detached process may take to post a payload before it is killed
constexpr auto async_post_limit = std::chrono::seconds(30);

void post_elastic(const std::vector<std::string>& payload,
                  const std::string& url, bool subproc) {
    if (subproc) {
        // Post the results from a detached process, which does not use
        // stdin/stdout/stderr or any other descriptor of this process: they
        // do not play nicely with Slurm, particularly with the --pty flag
        // and srun.
        // stdout/stderr are kept if trace logging is enabled, so that it is
        // still possble to get trace curl output.
        const bool keep_output = spdlog::get_level() <= spdlog::level::debug;
        util::spawn_detached(
            [&payload, &url, keep_output]() {
                if (!keep_output) {
                    spdlog::set_level(spdlog::level::off);
                    spdlog::set_error_handler([](const std::string&) {});
                }
                for (auto& text : payload) {
                    // use 10s timeout
                    if (auto result = util::curl::post(
                            text, url, "application/json", 10000);
                        !result) {
                        spdlog::warn("post_elastic: {}",
                                     result.error().message);
                        break;
                    }
                    spdlog::debug(
                        "post_elastic telemetry asynchronously to {}: {}", url,
                        text);
                }
            },
            async_post_limit,
            keep_output ? util::detached_output::keep
                        : util::detached_output::null);
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
