#include <memory>

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/syslog_sink.h"
#include <spdlog/spdlog.h>

#include "log.h"

namespace uenv {
void init_log(spdlog::level::level_enum console_log_level) {
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_st>();
    if (console_log_level >= spdlog::level::level_enum::info) {
        console_sink->set_pattern("[%^%l%$] %v");
    } else {
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    }
    // Make it the default logger
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>("uenv", console_sink));
    spdlog::set_level(console_log_level);
}
} // namespace uenv
