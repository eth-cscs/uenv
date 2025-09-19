#include <charconv>

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include <uenv/log.h>

static struct unit_init_log {
    unit_init_log() {
        // disable all logging so that tests that check erroneuous inputs don't
        // spam error and warning messages
        // The following will turn on warning and error logs:
        // export UENV_LOG_LEVEL=0
        auto log_level = spdlog::level::off;
        bool invalid_env = false;

        // check the environment variable UENV_LOG_LEVEL
        auto log_level_str = std::getenv("UENV_LOG_LEVEL");
        if (log_level_str != nullptr) {
            int lvl;
            auto [ptr, ec] = std::from_chars(
                log_level_str, log_level_str + std::strlen(log_level_str), lvl);

            if (ec == std::errc()) {
                if (lvl == 0) {
                    log_level = spdlog::level::warn;
                } else if (lvl == 1) {
                    log_level = spdlog::level::info;
                } else if (lvl == 2) {
                    log_level = spdlog::level::debug;
                } else if (lvl > 2) {
                    log_level = spdlog::level::trace;
                }
            } else {
                invalid_env = true;
            }
        }
        uenv::init_log(log_level);
        if (invalid_env) {
            spdlog::warn(fmt::format("UENV_LOG_LEVEL invalid value '{}' -- "
                                     "expected a value between 0 and 3",
                                     log_level_str));
        }
    }
} uil{};
