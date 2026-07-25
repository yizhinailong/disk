/**
 * @file LogHelper.hpp
 * @brief Structured application logging and typed correlation context
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <spdlog/common.h>

namespace spdlog {
    class logger;
}

namespace disk::utils {

    struct LogContext final {
        std::optional<std::string> request_id;
        std::optional<std::string> operation;
        std::optional<std::string> upload_id;
        std::optional<uint64_t> job_id;
        std::optional<std::string> lease_owner;
        std::optional<uint64_t> state_version;
    };

    [[nodiscard]] inline auto ServiceRuntimeLogContext() -> LogContext {
        return { .operation = "service_runtime" };
    }

    class LogStream final {
    public:
        explicit LogStream(
            spdlog::level::level_enum level,
            LogContext context
        );

        ~LogStream() noexcept;

        LogStream(const LogStream&) = delete;
        auto operator=(const LogStream&) -> LogStream& = delete;

        template <typename T>
        auto operator<<(const T& value) -> LogStream& {
            m_stream << value;
            return *this;
        }

    private:
        spdlog::level::level_enum m_level;
        LogContext m_context;
        std::ostringstream m_stream;
    };

    class Logger final {
    public:
        static auto Init(
            const std::string& log_file = "build/log/disk.log",
            size_t max_file_size = 10485760,
            size_t max_files = 10
        ) -> void;

        static auto ApplyStructuredFormatter(
            const std::shared_ptr<spdlog::logger>& logger
        ) -> void;

        static auto CaptureFrameworkLogs() -> void;

        static auto SetInstanceId(std::string instance_id) -> void;

        static auto Trace(LogContext context) -> LogStream;
        static auto Debug(LogContext context) -> LogStream;
        static auto Info(LogContext context) -> LogStream;
        static auto Warn(LogContext context) -> LogStream;
        static auto Error(LogContext context) -> LogStream;
        static auto Fatal(LogContext context) -> LogStream;

        static auto HighVolumeDetail(LogContext context) -> LogStream;
        static auto HighVolumeSuccess(LogContext context) -> LogStream;
        static auto HighVolumeFailure(LogContext context) -> LogStream;
    };

} // namespace disk::utils

namespace disk {
    using utils::Logger;
}
