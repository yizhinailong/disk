#include "utils/LogHelper.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

namespace disk::utils::test {

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

    TEST_F(LogHelperTest, InfoLevelDropsHighVolumeSuccessAndKeepsFailures) {
        m_logger->set_level(spdlog::level::info);

        Logger::HighVolumeDetail() << "chunk-detail";
        Logger::HighVolumeSuccess() << "chunk-success";
        Logger::HighVolumeFailure() << "chunk-failure";

        const auto output = Output();
        EXPECT_EQ(output.find("chunk-detail"), std::string::npos);
        EXPECT_EQ(output.find("chunk-success"), std::string::npos);
        EXPECT_NE(output.find("chunk-failure"), std::string::npos);
    }

    TEST_F(LogHelperTest, DebugLevelKeepsHighVolumeDetailsAndSuccesses) {
        m_logger->set_level(spdlog::level::debug);

        Logger::HighVolumeDetail() << "chunk-detail";
        Logger::HighVolumeSuccess() << "chunk-success";
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

        Logger::Info() << "early startup";

        const auto records = Records();
        ASSERT_EQ(records.size(), 1U);
        EXPECT_TRUE(records.front()["instance_id"].isNull());
        EXPECT_TRUE(records.front()["request_id"].isNull());
        EXPECT_TRUE(records.front()["upload_id"].isNull());
    }

} // namespace disk::utils::test
