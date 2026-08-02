/**
 * @file LogHelper.cpp
 * @brief Structured application logging implementation
 */

#include "utils/LogHelper.hpp"

#include <atomic>
#include <chrono>
#include <string_view>
#include <utility>

#include <json/json.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>
#include <spdlog/formatter.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <trantor/utils/Logger.h>

namespace disk::utils {

    namespace {
        constexpr Json::Int64 LOG_SCHEMA_VERSION = 1;
        constexpr std::string_view APPLICATION_ENVELOPE_MARKER =
            "disk-application-log-v1";

        std::atomic<std::shared_ptr<const std::string>> g_instance_id;

        [[nodiscard]]
        auto SnapshotInstanceId() -> std::shared_ptr<const std::string> {
            return g_instance_id.load(std::memory_order_acquire);
        }

        auto SetNullableString(
            Json::Value& record,
            std::string_view name,
            const std::optional<std::string>& value
        ) -> void {
            record[std::string(name)] = value.has_value() && !value->empty() ?
                                            Json::Value(*value) :
                                            Json::Value(Json::nullValue);
        }

        auto SetNullableInteger(
            Json::Value& record,
            std::string_view name,
            const std::optional<uint64_t>& value
        ) -> void {
            record[std::string(name)] = value.has_value() ?
                                            Json::Value(Json::UInt64(*value)) :
                                            Json::Value(Json::nullValue);
        }

        auto SetInstanceId(Json::Value& record) -> void {
            const auto instance_id = SnapshotInstanceId();
            record["instance_id"] = instance_id != nullptr && !instance_id->empty() ?
                                        Json::Value(*instance_id) :
                                        Json::Value(Json::nullValue);
        }

        [[nodiscard]]
        auto LevelName(spdlog::level::level_enum level) -> std::string {
            const auto name = spdlog::level::to_string_view(level);
            return { name.data(), name.size() };
        }

        [[nodiscard]]
        auto FrameworkLogLevel() -> spdlog::level::level_enum {
            switch (trantor::Logger::logLevel()) {
                case trantor::Logger::kTrace:
                    return spdlog::level::trace;
                case trantor::Logger::kDebug:
                    return spdlog::level::debug;
                case trantor::Logger::kInfo:
                    return spdlog::level::info;
                case trantor::Logger::kWarn:
                    return spdlog::level::warn;
                case trantor::Logger::kError:
                    return spdlog::level::err;
                case trantor::Logger::kFatal:
                    return spdlog::level::critical;
                case trantor::Logger::kNumberOfLogLevels:
                    return spdlog::level::off;
            }
            return spdlog::level::info;
        }

        [[nodiscard]]
        auto MillisecondsSinceEpoch(spdlog::log_clock::time_point time) -> Json::Int64 {
            return static_cast<Json::Int64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    time.time_since_epoch()
                )
                    .count()
            );
        }

        [[nodiscard]]
        auto BuildBaseRecord(
            spdlog::log_clock::time_point time,
            spdlog::level::level_enum level,
            std::string_view logger_name,
            std::string_view source,
            std::string_view message
        ) -> Json::Value {
            Json::Value record(Json::objectValue);
            record["schema_version"] = LOG_SCHEMA_VERSION;
            record["timestamp_unix_ms"] = MillisecondsSinceEpoch(time);
            record["level"] = LevelName(level);
            record["source"] = std::string(source);
            record["logger"] = std::string(logger_name);
            record["message"] = std::string(message);
            record["request_id"] = Json::Value(Json::nullValue);
            SetInstanceId(record);
            record["operation"] = Json::Value(Json::nullValue);
            record["upload_id"] = Json::Value(Json::nullValue);
            record["job_id"] = Json::Value(Json::nullValue);
            record["lease_owner"] = Json::Value(Json::nullValue);
            record["state_version"] = Json::Value(Json::nullValue);
            return record;
        }

        auto ApplyContext(Json::Value& record, const LogContext& context) -> void {
            SetNullableString(record, "request_id", context.request_id);
            SetNullableString(record, "operation", context.operation);
            SetNullableString(record, "upload_id", context.upload_id);
            SetNullableInteger(record, "job_id", context.job_id);
            SetNullableString(record, "lease_owner", context.lease_owner);
            SetNullableInteger(record, "state_version", context.state_version);
        }

        [[nodiscard]]
        auto Serialize(const Json::Value& record) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            builder["emitUTF8"] = true;
            return Json::writeString(builder, record);
        }

        [[nodiscard]]
        auto ParseApplicationEnvelope(
            std::string_view payload,
            Json::Value& envelope
        ) -> bool {
            if (payload.empty() || payload.front() != '{') {
                return false;
            }

            Json::CharReaderBuilder builder;
            builder["collectComments"] = false;
            const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
            std::string errors;
            if (!reader->parse(
                    payload.data(),
                    payload.data() + payload.size(),
                    &envelope,
                    &errors
                )) {
                return false;
            }

            return envelope.isObject() &&
                   envelope["_disk_internal_envelope"].isString() &&
                   envelope["_disk_internal_envelope"].asString() ==
                       APPLICATION_ENVELOPE_MARKER;
        }

        auto AppendLine(spdlog::memory_buf_t& destination, std::string_view line) -> void {
            destination.append(line.data(), line.data() + line.size());
            const std::string_view end_of_line(spdlog::details::os::default_eol);
            destination.append(
                end_of_line.data(),
                end_of_line.data() + end_of_line.size()
            );
        }

        class StructuredLogFormatter final : public spdlog::formatter {
        public:
            auto format(
                const spdlog::details::log_msg& message,
                spdlog::memory_buf_t& destination
            ) -> void override {
                const std::string_view payload(message.payload.data(), message.payload.size());
                const std::string_view logger_name(
                    message.logger_name.data(),
                    message.logger_name.size()
                );

                Json::Value envelope;
                Json::Value record;
                if (ParseApplicationEnvelope(payload, envelope)) {
                    record = BuildBaseRecord(
                        message.time,
                        message.level,
                        logger_name,
                        "application",
                        envelope["message"].asString()
                    );
                    for (const auto* field : {
                             "request_id",
                             "operation",
                             "upload_id",
                             "job_id",
                             "lease_owner",
                             "state_version",
                         }) {
                        if (envelope.isMember(field)) {
                            record[field] = envelope[field];
                        }
                    }
                } else {
                    record = BuildBaseRecord(
                        message.time,
                        message.level,
                        logger_name,
                        "framework",
                        payload
                    );
                }

                AppendLine(destination, Serialize(record));
            }

            [[nodiscard]]
            auto clone() const -> std::unique_ptr<spdlog::formatter> override {
                return std::make_unique<StructuredLogFormatter>();
            }
        };
    } // namespace

    auto SetRequestCorrelationFields(Json::Value& details, const LogContext& context)
        -> void {
        SetNullableString(details, "request_id", context.request_id);
        SetNullableString(details, "operation", context.operation);
    }

    LogStream::LogStream(spdlog::level::level_enum level, LogContext context)
        : m_level(level), m_context(std::move(context)) {
    }

    LogStream::~LogStream() noexcept {
        const auto logger = spdlog::default_logger();
        if (logger == nullptr || !logger->should_log(m_level)) {
            return;
        }

        try {
            auto envelope = BuildBaseRecord(
                spdlog::log_clock::now(),
                m_level,
                logger->name(),
                "application",
                m_stream.str()
            );
            ApplyContext(envelope, m_context);
            envelope["_disk_internal_envelope"] =
                std::string(APPLICATION_ENVELOPE_MARKER);
            logger->log(m_level, "{}", Serialize(envelope));
        } catch (...) {
            try {
                logger->log(m_level, "Structured application log serialization failed");
            } catch (...) {
            }
        }
    }

    auto Logger::Init(
        const std::string& log_file,
        size_t max_file_size,
        size_t max_files
    ) -> void {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file,
            max_file_size,
            max_files
        );
        auto logger = std::make_shared<spdlog::logger>(
            "disk",
            spdlog::sinks_init_list{ console_sink, file_sink }
        );
        ApplyStructuredFormatter(logger);
        logger->flush_on(spdlog::level::err);
        spdlog::set_default_logger(std::move(logger));
    }

    auto Logger::ApplyStructuredFormatter(
        const std::shared_ptr<spdlog::logger>& logger
    ) -> void {
        if (logger != nullptr) {
            logger->set_formatter(std::make_unique<StructuredLogFormatter>());
        }
    }

    auto Logger::CaptureFrameworkLogs() -> void {
        trantor::Logger::enableSpdLog(-1, spdlog::default_logger());
    }

    auto Logger::SyncFrameworkLevel() -> void {
        const auto logger = spdlog::default_logger();
        if (logger != nullptr) {
            logger->set_level(FrameworkLogLevel());
        }
    }

    auto Logger::SetInstanceId(std::string instance_id) -> void {
        if (instance_id.empty()) {
            g_instance_id.store(nullptr, std::memory_order_release);
            return;
        }
        g_instance_id.store(
            std::make_shared<const std::string>(std::move(instance_id)),
            std::memory_order_release
        );
    }

    auto Logger::Trace(LogContext context) -> LogStream {
        return LogStream(spdlog::level::trace, std::move(context));
    }

    auto Logger::Debug(LogContext context) -> LogStream {
        return LogStream(spdlog::level::debug, std::move(context));
    }

    auto Logger::Info(LogContext context) -> LogStream {
        return LogStream(spdlog::level::info, std::move(context));
    }

    auto Logger::Warn(LogContext context) -> LogStream {
        return LogStream(spdlog::level::warn, std::move(context));
    }

    auto Logger::Error(LogContext context) -> LogStream {
        return LogStream(spdlog::level::err, std::move(context));
    }

    auto Logger::Fatal(LogContext context) -> LogStream {
        return LogStream(spdlog::level::critical, std::move(context));
    }

    auto Logger::HighVolumeDetail(LogContext context) -> LogStream {
        return Debug(std::move(context));
    }

    auto Logger::HighVolumeSuccess(LogContext context) -> LogStream {
        return Debug(std::move(context));
    }

    auto Logger::HighVolumeFailure(LogContext context) -> LogStream {
        return Info(std::move(context));
    }

} // namespace disk::utils
