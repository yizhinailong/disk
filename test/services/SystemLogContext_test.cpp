/**
 * @file SystemLogContext_test.cpp
 * @brief System information request correlation contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "services/SystemService.hpp"
#include "utils/LogHelper.hpp"
#include "utils/StageTimer.hpp"

namespace disk::system {
    namespace {

        auto RepositoryRoot() -> std::filesystem::path {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        }

        auto ReadSourceFile(const std::filesystem::path& relative_path) -> std::string {
            std::ifstream input(RepositoryRoot() / relative_path);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        auto Contains(const std::string& source, std::string_view expected) -> bool {
            return source.find(expected) != std::string::npos;
        }

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        auto ExtractRange(
            const std::string& source,
            std::string_view begin_marker,
            std::string_view end_marker
        ) -> std::string {
            const auto begin = source.find(begin_marker);
            const auto end = source.find(end_marker, begin);
            if (begin == std::string::npos || end == std::string::npos) {
                return {};
            }
            return source.substr(begin, end - begin);
        }

        template <typename Service>
        concept HasContextOnlyGetInfo = requires(Service& service) {
            service.GetInfo(disk::utils::LogContext{});
        };

        template <typename Service>
        concept HasUserScopedGetInfo = requires(Service& service) {
            service.GetInfo(uint64_t{ 1 }, disk::utils::LogContext{});
        };

        TEST(SystemServiceContractTest, RetainsOnlyUsedRequestAndInfrastructureInputs) {
            EXPECT_TRUE(HasContextOnlyGetInfo<SystemService>);
            EXPECT_FALSE(HasUserScopedGetInfo<SystemService>);
        }

        TEST(SystemBuildTimeContractTest, FormatterHasInternalLinkage) {
            const auto service_header = ReadSourceFile("src/services/SystemService.hpp");
            const auto service_source = ReadSourceFile("src/services/SystemService.cpp");

            EXPECT_FALSE(Contains(service_header, "GetBuildTime"));
            EXPECT_FALSE(Contains(service_source, "SystemService::GetBuildTime"));
            EXPECT_TRUE(Contains(
                service_source,
                "[[nodiscard]] auto GetBuildTime() -> std::string"
            ));
            EXPECT_TRUE(Contains(service_source, "info.build_time = GetBuildTime()"));
            EXPECT_TRUE(Contains(
                service_source,
                "std::string(__DATE__) + \" \" + std::string(__TIME__)"
            ));
        }

        TEST(SystemConnectionStatsContractTest, CollectorHasInternalLinkageAndKeepsFieldSources) {
            const auto service_header = ReadSourceFile("src/services/SystemService.hpp");
            const auto service_source = ReadSourceFile("src/services/SystemService.cpp");

            EXPECT_FALSE(Contains(service_header, "GetConnectionStats"));
            EXPECT_FALSE(Contains(service_source, "SystemService::GetConnectionStats"));
            EXPECT_TRUE(Contains(
                service_source,
                "[[nodiscard]] auto GetConnectionStats() -> drogon::Task<ConnectionStats>"
            ));
            EXPECT_EQ(CountOccurrences(service_source, "GetConnectionStats()"), 2U);
            EXPECT_TRUE(Contains(
                service_source,
                "info.connections = co_await GetConnectionStats()"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "stats.db_pool_size = config->GetDbPoolSize()"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "stats.redis_pool_size = config->GetRedisPoolSize()"
            ));
            EXPECT_TRUE(Contains(service_source, "stats.current = stats.db_pool_size"));
            EXPECT_TRUE(Contains(service_source, "stats.peak = stats.db_pool_size"));
        }

        TEST(SystemLogContextContractTest, ControllerServiceAndStageTimerKeepExplicitContext) {
            const auto controller_source = ReadSourceFile("src/controllers/SystemController.cpp");
            const auto service_header = ReadSourceFile("src/services/SystemService.hpp");
            const auto service_source = ReadSourceFile("src/services/SystemService.cpp");
            const auto stage_timer_source = ReadSourceFile("src/utils/StageTimer.hpp");
            const auto service_request_body = ExtractRange(
                service_source,
                "auto SystemService::GetInfo(",
                "\n} // namespace disk::system"
            );

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(service_request_body.empty());
            ASSERT_FALSE(stage_timer_source.empty());

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"system_info\")"
                ),
                1
            );
            EXPECT_TRUE(Contains(controller_source, "attributes()->find(\"user_id\")"));
            EXPECT_TRUE(Contains(controller_source, "ErrorCode::TokenMissing"));
            EXPECT_FALSE(Contains(controller_source, "attributes()->get<uint64_t>(\"user_id\")"));
            EXPECT_TRUE(Contains(controller_source, "m_system_service->GetInfo(log_context)"));
            EXPECT_FALSE(Contains(controller_source, "getRedisClient()"));
            EXPECT_FALSE(Contains(service_header, "RedisClient"));
            EXPECT_FALSE(Contains(service_header, "m_redis_client"));
            EXPECT_TRUE(Contains(
                service_header,
                "disk::utils::LogContext log_context = {}"
            ));
            EXPECT_TRUE(Contains(
                service_request_body,
                "StageTimer timer(\"system_get_info\", log_context)"
            ));
            EXPECT_TRUE(Contains(service_request_body, "GetStorageStats(log_context)"));
            EXPECT_TRUE(Contains(service_request_body, "Logger::Error(log_context)"));
            EXPECT_EQ(CountOccurrences(service_request_body, ".what()"), 0U);
            EXPECT_TRUE(Contains(
                service_request_body,
                "Logger::Error(log_context) << \"Failed to get storage stats\";"
            ));
            EXPECT_TRUE(Contains(stage_timer_source, "LogContext log_context = {}"));
            EXPECT_TRUE(Contains(
                stage_timer_source,
                "m_log_context(std::move(log_context))"
            ));
            EXPECT_TRUE(Contains(stage_timer_source, "Logger::Info(m_log_context)"));

            for (const auto* body : {
                     &controller_source,
                     &service_request_body,
                     &stage_timer_source,
                 }) {
                EXPECT_FALSE(Contains(*body, "Logger::Debug()"));
                EXPECT_FALSE(Contains(*body, "Logger::Info()"));
                EXPECT_FALSE(Contains(*body, "Logger::Warn()"));
                EXPECT_FALSE(Contains(*body, "Logger::Error()"));
            }

            const auto previous_logger = spdlog::default_logger();
            std::ostringstream output;
            const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
            const auto logger = std::make_shared<spdlog::logger>("stage-timer-test", sink);
            disk::utils::Logger::ApplyStructuredFormatter(logger);
            logger->set_level(spdlog::level::info);
            spdlog::set_default_logger(logger);
            disk::utils::Logger::SetInstanceId("disk-stage-timer-instance");

            {
                disk::utils::StageTimer timer(
                    "system_get_info",
                    disk::utils::LogContext{
                        .request_id = "request-123",
                        .operation = "system_info",
                        .upload_id = "upload-456",
                        .job_id = 42,
                        .lease_owner = "worker-7",
                        .state_version = 9,
                    }
                );
            }
            logger->flush();
            disk::utils::Logger::SetInstanceId("");
            spdlog::set_default_logger(previous_logger);

            std::string line;
            std::istringstream lines(output.str());
            ASSERT_TRUE(static_cast<bool>(std::getline(lines, line)));
            std::string extra_line;
            EXPECT_FALSE(static_cast<bool>(std::getline(lines, extra_line)));

            Json::CharReaderBuilder builder;
            builder["collectComments"] = false;
            const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
            Json::Value record;
            std::string errors;
            ASSERT_TRUE(reader->parse(
                line.data(),
                line.data() + line.size(),
                &record,
                &errors
            )) << errors;

            EXPECT_EQ(record["schema_version"].asInt64(), 1);
            EXPECT_EQ(record["level"].asString(), "info");
            EXPECT_EQ(record["source"].asString(), "application");
            EXPECT_EQ(record["message"].asString().find("[stage_timer] system_get_info"), 0U);
            EXPECT_EQ(record["request_id"].asString(), "request-123");
            EXPECT_EQ(record["instance_id"].asString(), "disk-stage-timer-instance");
            EXPECT_EQ(record["operation"].asString(), "system_info");
            EXPECT_EQ(record["upload_id"].asString(), "upload-456");
            EXPECT_EQ(record["job_id"].asUInt64(), 42U);
            EXPECT_EQ(record["lease_owner"].asString(), "worker-7");
            EXPECT_EQ(record["state_version"].asUInt64(), 9U);
        }

    } // namespace
} // namespace disk::system
