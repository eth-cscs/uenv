#include <memory>

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/syslog_sink.h"
#include <spdlog/spdlog.h>

#include "log.h"

namespace uenv {
void init_log() {
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_st>();
    console_sink->set_level(spdlog::level::warn);

    auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_st>(
        "uenv", LOG_PID, 0, false);
    syslog_sink->set_level(spdlog::level::trace);

    spdlog::set_default_logger(std::make_shared<spdlog::logger>(
        "uenv", spdlog::sinks_init_list({console_sink, syslog_sink})));
}
} // namespace uenv
