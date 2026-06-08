#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <sstream>
#include <trantor/utils/Logger.h>

namespace disk::utils {

class LogStream {
public:
    explicit LogStream(spdlog::level::level_enum level) : level_(level) {}

    ~LogStream() {
        spdlog::default_logger_raw()->log(level_, "{}", oss_.str());
    }

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;

    template <typename T>
    LogStream& operator<<(const T& val) {
        oss_ << val;
        return *this;
    }

private:
    spdlog::level::level_enum level_;
    std::ostringstream oss_;
};

class Logger {
public:
    static void Init(const std::string& log_file = "build/log/disk.log",
                     size_t max_file_size = 10485760,
                     size_t max_files = 10) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, max_file_size, max_files);
        auto logger = std::make_shared<spdlog::logger>(
            "disk", spdlog::sinks_init_list{console_sink, file_sink});
        logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] %v");
        logger->flush_on(spdlog::level::err);
        spdlog::set_default_logger(logger);
    }

    static void CaptureFrameworkLogs() {
        trantor::Logger::enableSpdLog(-1, spdlog::default_logger());
    }

    static LogStream Trace() { return LogStream(spdlog::level::trace); }
    static LogStream Debug() { return LogStream(spdlog::level::debug); }
    static LogStream Info() { return LogStream(spdlog::level::info); }
    static LogStream Warn() { return LogStream(spdlog::level::warn); }
    static LogStream Error() { return LogStream(spdlog::level::err); }
    static LogStream Fatal() { return LogStream(spdlog::level::critical); }
};

} ///< namespace disk::utils

/// Import Logger into disk namespace so it's accessible as Logger:: inside disk::*
namespace disk { using utils::Logger; }
