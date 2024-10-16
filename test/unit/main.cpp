#include <charconv>

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include <uenv/log.h>

static struct unit_init_log {
    unit_init_log() {
        // use warn as the default log level
        auto log_level = spdlog::level::warn;
        bool invalid_env = false;

        // check the environment variable UENV_LOG_LEVEL
        auto log_level_str = std::getenv("UENV_LOG_LEVEL");
        if (log_level_str != nullptr) {
            int lvl;
            auto [ptr, ec] = std::from_chars(
                log_level_str, log_level_str + std::strlen(log_level_str), lvl);

            if (ec == std::errc()) {
                if (lvl == 1) {
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
        uenv::init_log(log_level, spdlog::level::info);
        if (invalid_env) {
            spdlog::warn(fmt::format("UENV_LOG_LEVEL invalid value '{}' -- "
                                     "expected a value between 0 and 3",
                                     log_level_str));
        }
    }
} uil{};
