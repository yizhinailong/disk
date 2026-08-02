#include "utils/LogHelper.hpp"

#include <array>
#include <concepts>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <trantor/utils/Logger.h>

namespace disk::utils::test {

    template <typename LoggerType>
    concept HasContextFreeLoggerEntry =
        requires { LoggerType::Trace(); } ||
        requires { LoggerType::Debug(); } ||
        requires { LoggerType::Info(); } ||
        requires { LoggerType::Warn(); } ||
        requires { LoggerType::Error(); } ||
        requires { LoggerType::Fatal(); } ||
        requires { LoggerType::HighVolumeDetail(); } ||
        requires { LoggerType::HighVolumeSuccess(); } ||
        requires { LoggerType::HighVolumeFailure(); };

    template <typename LoggerType>
    concept HasExplicitLoggerEntries = requires(LogContext context) {
        { LoggerType::Trace(context) } -> std::same_as<LogStream>;
        { LoggerType::Debug(context) } -> std::same_as<LogStream>;
        { LoggerType::Info(context) } -> std::same_as<LogStream>;
        { LoggerType::Warn(context) } -> std::same_as<LogStream>;
        { LoggerType::Error(context) } -> std::same_as<LogStream>;
        { LoggerType::Fatal(context) } -> std::same_as<LogStream>;
        { LoggerType::HighVolumeDetail(context) } -> std::same_as<LogStream>;
        { LoggerType::HighVolumeSuccess(context) } -> std::same_as<LogStream>;
        { LoggerType::HighVolumeFailure(context) } -> std::same_as<LogStream>;
    };

    static_assert(!HasContextFreeLoggerEntry<Logger>);
    static_assert(HasExplicitLoggerEntries<Logger>);
    static_assert(!std::constructible_from<LogStream, spdlog::level::level_enum>);
    static_assert(std::constructible_from<LogStream, spdlog::level::level_enum, LogContext>);

    class LogHelperTest : public ::testing::Test {
    protected:
        auto SetUp() -> void override {
            m_previous_logger = spdlog::default_logger();
            m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
            m_logger = std::make_shared<spdlog::logger>("log-helper-test", m_sink);
            Logger::ApplyStructuredFormatter(m_logger);
            spdlog::set_default_logger(m_logger);
            Logger::SetInstanceId("disk-test-instance");
        }

        auto TearDown() -> void override {
            Logger::SetInstanceId("");
            spdlog::set_default_logger(m_previous_logger);
        }

        [[nodiscard]] auto Output() -> std::string {
            m_logger->flush();
            return m_output.str();
        }

        [[nodiscard]] auto Records() -> std::vector<Json::Value> {
            std::vector<Json::Value> records;
            std::istringstream lines(Output());
            std::string line;
            while (std::getline(lines, line)) {
                if (line.empty()) {
                    continue;
                }

                Json::CharReaderBuilder builder;
                builder["collectComments"] = false;
                const auto reader = std::unique_ptr<Json::CharReader>(
                    builder.newCharReader()
                );
                Json::Value record;
                std::string errors;
                if (!reader->parse(
                        line.data(),
                        line.data() + line.size(),
                        &record,
                        &errors
                    )) {
                    ADD_FAILURE() << "Invalid structured log line: " << errors;
                    continue;
                }
                records.push_back(std::move(record));
            }
            return records;
        }

        std::shared_ptr<spdlog::logger> m_previous_logger;
        std::ostringstream m_output;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
        std::shared_ptr<spdlog::logger> m_logger;
    };

    TEST_F(LogHelperTest, RequestCorrelationFieldsPreserveValuesAndNullSemantics) {
        Json::Value details(Json::objectValue);
        details["domain_field"] = "preserved";
        SetRequestCorrelationFields(
            details,
            LogContext{
                .request_id = "request-123",
                .operation = "admin",
                .upload_id = "upload-ignored",
                .job_id = 42,
                .lease_owner = "owner-ignored",
                .state_version = 7,
            }
        );

        EXPECT_EQ(details["request_id"].asString(), "request-123");
        EXPECT_EQ(details["operation"].asString(), "admin");
        EXPECT_EQ(details["domain_field"].asString(), "preserved");
        EXPECT_FALSE(details.isMember("upload_id"));
        EXPECT_FALSE(details.isMember("job_id"));
        EXPECT_FALSE(details.isMember("lease_owner"));
        EXPECT_FALSE(details.isMember("state_version"));

        Json::Value empty_details(Json::objectValue);
        SetRequestCorrelationFields(
            empty_details,
            LogContext{ .request_id = "", .operation = "" }
        );
        EXPECT_TRUE(empty_details["request_id"].isNull());
        EXPECT_TRUE(empty_details["operation"].isNull());

        Json::Value missing_details(Json::objectValue);
        SetRequestCorrelationFields(missing_details, LogContext{});
        EXPECT_TRUE(missing_details["request_id"].isNull());
        EXPECT_TRUE(missing_details["operation"].isNull());
    }

    TEST_F(LogHelperTest, InfoLevelDropsHighVolumeSuccessAndKeepsFailures) {
        m_logger->set_level(spdlog::level::info);

        Logger::HighVolumeDetail(LogContext{}) << "chunk-detail";
        Logger::HighVolumeSuccess(LogContext{}) << "chunk-success";
        Logger::HighVolumeFailure(LogContext{}) << "chunk-failure";

        const auto output = Output();
        EXPECT_EQ(output.find("chunk-detail"), std::string::npos);
        EXPECT_EQ(output.find("chunk-success"), std::string::npos);
        EXPECT_NE(output.find("chunk-failure"), std::string::npos);
    }

    TEST_F(LogHelperTest, DebugLevelKeepsHighVolumeDetailsAndSuccesses) {
        m_logger->set_level(spdlog::level::debug);

        Logger::HighVolumeDetail(LogContext{}) << "chunk-detail";
        Logger::HighVolumeSuccess(LogContext{}) << "chunk-success";
        Logger::Debug(ServiceRuntimeLogContext()) << "Service initialized: service=test";

        const auto output = Output();
        EXPECT_NE(output.find("chunk-detail"), std::string::npos);
        EXPECT_NE(output.find("chunk-success"), std::string::npos);

        const auto records = Records();
        ASSERT_EQ(records.size(), 3U);
        const auto& runtime_record = records.back();
        EXPECT_EQ(runtime_record["level"].asString(), "debug");
        EXPECT_EQ(runtime_record["instance_id"].asString(), "disk-test-instance");
        EXPECT_EQ(runtime_record["operation"].asString(), "service_runtime");
        EXPECT_EQ(runtime_record["message"].asString(), "Service initialized: service=test");
        for (const auto* field : {
                 "request_id",
                 "upload_id",
                 "job_id",
                 "lease_owner",
                 "state_version",
             }) {
            EXPECT_TRUE(runtime_record[field].isNull()) << field;
        }
    }

    TEST_F(LogHelperTest, SynchronizesFrameworkLogLevel) {
        const auto previous_level = trantor::Logger::logLevel();
        constexpr std::array LEVELS{
            std::pair{ trantor::Logger::kTrace,    spdlog::level::trace },
            std::pair{ trantor::Logger::kDebug,    spdlog::level::debug },
            std::pair{  trantor::Logger::kInfo,     spdlog::level::info },
            std::pair{  trantor::Logger::kWarn,     spdlog::level::warn },
            std::pair{ trantor::Logger::kError,      spdlog::level::err },
            std::pair{ trantor::Logger::kFatal, spdlog::level::critical },
        };

        for (const auto& [framework_level, expected_level] : LEVELS) {
            trantor::Logger::setLogLevel(framework_level);
            Logger::SyncFrameworkLevel();
            EXPECT_EQ(m_logger->level(), expected_level);
        }
        trantor::Logger::setLogLevel(previous_level);
    }

    TEST_F(LogHelperTest, ApplicationLogEmitsTypedCorrelationContext) {
        m_logger->set_level(spdlog::level::info);

        Logger::Info(LogContext{
            .request_id = "request-123",
            .operation = "upload_complete",
            .upload_id = "upload-456",
            .job_id = 42,
            .lease_owner = "worker-7",
            .state_version = 9,
        }) << "quoted \"message\"\nnext line";

        const auto records = Records();
        ASSERT_EQ(records.size(), 1U);
        const auto& record = records.front();
        EXPECT_EQ(record["schema_version"].asInt64(), 1);
        EXPECT_GT(record["timestamp_unix_ms"].asInt64(), 0);
        EXPECT_EQ(record["level"].asString(), "info");
        EXPECT_EQ(record["source"].asString(), "application");
        EXPECT_EQ(record["logger"].asString(), "log-helper-test");
        EXPECT_EQ(record["message"].asString(), "quoted \"message\"\nnext line");
        EXPECT_EQ(record["request_id"].asString(), "request-123");
        EXPECT_EQ(record["instance_id"].asString(), "disk-test-instance");
        EXPECT_EQ(record["operation"].asString(), "upload_complete");
        EXPECT_EQ(record["upload_id"].asString(), "upload-456");
        EXPECT_EQ(record["job_id"].asUInt64(), 42U);
        EXPECT_EQ(record["lease_owner"].asString(), "worker-7");
        EXPECT_EQ(record["state_version"].asUInt64(), 9U);
    }

    TEST_F(LogHelperTest, FrameworkLogUsesNullForUnavailableContext) {
        m_logger->set_level(spdlog::level::info);

        m_logger->warn("framework \"message\"\nnext line");

        const auto records = Records();
        ASSERT_EQ(records.size(), 1U);
        const auto& record = records.front();
        EXPECT_EQ(record["schema_version"].asInt64(), 1);
        EXPECT_EQ(record["level"].asString(), "warning");
        EXPECT_EQ(record["source"].asString(), "framework");
        EXPECT_EQ(record["logger"].asString(), "log-helper-test");
        EXPECT_EQ(record["message"].asString(), "framework \"message\"\nnext line");
        EXPECT_EQ(record["instance_id"].asString(), "disk-test-instance");
        for (const auto* field : {
                 "request_id",
                 "operation",
                 "upload_id",
                 "job_id",
                 "lease_owner",
                 "state_version",
             }) {
            EXPECT_TRUE(record.isMember(field));
            EXPECT_TRUE(record[field].isNull()) << field;
        }
    }

    TEST_F(LogHelperTest, EarlyLogKeepsUnknownInstanceAsJsonNull) {
        m_logger->set_level(spdlog::level::info);
        Logger::SetInstanceId("");

        Logger::Info(LogContext{}) << "early startup";

        const auto records = Records();
        ASSERT_EQ(records.size(), 1U);
        EXPECT_TRUE(records.front()["instance_id"].isNull());
        EXPECT_TRUE(records.front()["request_id"].isNull());
        EXPECT_TRUE(records.front()["upload_id"].isNull());
    }

} // namespace disk::utils::test
