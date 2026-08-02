#include "services/HealthService.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::health {
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

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        auto IsUtcIsoSeconds(const std::string& value) -> bool {
            if (value.size() != 20 || value.back() != 'Z') {
                return false;
            }

            std::tm parsed{};
            std::istringstream stream(value);
            stream >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%SZ");
            return !stream.fail() && stream.peek() == std::char_traits<char>::eof();
        }

        struct CheckCounts {
            size_t database{ 0 };
            size_t redis{ 0 };
            size_t staging_storage{ 0 };
            size_t final_storage{ 0 };
            size_t storage_jobs{ 0 };
        };

        [[nodiscard]] auto HealthyCheck(size_t& calls) -> ComponentCheck {
            return [&calls](disk::utils::LogContext) -> drogon::Task<ComponentStatus> {
                calls++;
                co_return ComponentStatus{ .status = "healthy", .latency_ms = 1 };
            };
        }

        [[nodiscard]] auto HealthyChecks(CheckCounts& counts) -> HealthCheckCallbacks {
            return HealthCheckCallbacks{
                .database = HealthyCheck(counts.database),
                .redis = HealthyCheck(counts.redis),
                .staging_storage = HealthyCheck(counts.staging_storage),
                .final_storage = HealthyCheck(counts.final_storage),
                .storage_jobs = HealthyCheck(counts.storage_jobs),
            };
        }

        TEST(HealthServiceTimestampContractTest, FormatterHasInternalLinkage) {
            const auto header = ReadSourceFile("src/services/HealthService.hpp");
            const auto source = ReadSourceFile("src/services/HealthService.cpp");

            EXPECT_EQ(header.find("GetTimestamp"), std::string::npos);
            EXPECT_EQ(source.find("HealthService::GetTimestamp"), std::string::npos);
            EXPECT_NE(
                source.find("[[nodiscard]] auto GetTimestamp() -> std::string"),
                std::string::npos
            );
            EXPECT_NE(source.find(".timestamp = GetTimestamp(),"), std::string::npos);
            EXPECT_NE(source.find("std::chrono::system_clock::now()"), std::string::npos);
            EXPECT_NE(source.find("gmtime_s(&utc, &timestamp)"), std::string::npos);
            EXPECT_NE(source.find("gmtime_r(&timestamp, &utc)"), std::string::npos);
            EXPECT_NE(source.find("%Y-%m-%dT%H:%M:%SZ"), std::string::npos);
        }

        TEST(HealthServiceTimestampContractTest, BothProbesUseUtcIsoSeconds) {
            CheckCounts counts;
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::Api,
                "api-timestamp"
            );
            runtime->MarkInitialized();
            HealthService service(runtime, HealthyChecks(counts));

            const auto liveness = service.CheckLiveness();
            const auto readiness = drogon::sync_wait(service.CheckReadiness());

            EXPECT_TRUE(IsUtcIsoSeconds(liveness.timestamp));
            EXPECT_TRUE(IsUtcIsoSeconds(readiness.timestamp));
            EXPECT_EQ(liveness.ToJson()["timestamp"].asString(), liveness.timestamp);
            EXPECT_EQ(readiness.ToJson()["timestamp"].asString(), readiness.timestamp);
        }

        TEST(HealthServiceHelperContractTest, ComponentRunnerHasInternalLinkage) {
            const auto header = ReadSourceFile("src/services/HealthService.hpp");
            const auto source = ReadSourceFile("src/services/HealthService.cpp");

            EXPECT_EQ(header.find("RunComponentCheck"), std::string::npos);
            EXPECT_EQ(
                source.find("HealthService::RunComponentCheck"),
                std::string::npos
            );
            EXPECT_NE(source.find("auto RunComponentCheck("), std::string::npos);
            EXPECT_EQ(CountOccurrences(source, "RunComponentCheck("), 6U);
        }

        TEST(HealthControllerResponseContractTest, MapperHasInternalLinkage) {
            const auto header = ReadSourceFile("src/controllers/HealthController.hpp");
            const auto source = ReadSourceFile("src/controllers/HealthController.cpp");

            EXPECT_EQ(header.find("ToResponse("), std::string::npos);
            EXPECT_EQ(source.find("HealthController::ToResponse("), std::string::npos);
            EXPECT_NE(
                source.find("namespace {\n        [[nodiscard]] auto ToResponse("),
                std::string::npos
            );
            EXPECT_NE(
                source.find("[[nodiscard]] auto ToResponse(const HealthResult& result)"),
                std::string::npos
            );
            EXPECT_EQ(CountOccurrences(source, "ToResponse("), 3U);
            EXPECT_NE(
                source.find("Response::Success(result.ToJson())"),
                std::string::npos
            );
            EXPECT_NE(
                source.find("result.overall_status != \"healthy\""),
                std::string::npos
            );
            EXPECT_NE(
                source.find("response->setStatusCode(drogon::k503ServiceUnavailable)"),
                std::string::npos
            );
        }

        TEST(HealthServiceTest, LivenessNeverCallsExternalDependencies) {
            CheckCounts counts;
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::Api,
                "api-1"
            );
            HealthService service(runtime, HealthyChecks(counts));

            const auto result = service.CheckLiveness();

            EXPECT_EQ(result.overall_status, "healthy");
            EXPECT_EQ(result.role, "api");
            EXPECT_EQ(result.instance_id, "api-1");
            EXPECT_FALSE(result.initialized);
            EXPECT_EQ(counts.database, 0U);
            EXPECT_EQ(counts.redis, 0U);
            EXPECT_EQ(counts.staging_storage, 0U);
            EXPECT_EQ(counts.final_storage, 0U);
            EXPECT_EQ(counts.storage_jobs, 0U);
        }

        TEST(HealthServiceTest, ApiReadinessChecksRedisButNotStorageJobQueue) {
            CheckCounts counts;
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::Api,
                "api-1",
                true,
                false
            );
            runtime->MarkInitialized();
            ASSERT_TRUE(runtime->TryAcquireBusinessRequest());
            HealthService service(runtime, HealthyChecks(counts));

            const auto result = drogon::sync_wait(service.CheckReadiness());
            const auto json = result.ToJson();

            EXPECT_EQ(result.overall_status, "healthy");
            EXPECT_FALSE(result.worker_claiming_enabled);
            EXPECT_FALSE(result.worker_accepting);
            EXPECT_FALSE(result.upload_task_creation_enabled);
            EXPECT_EQ(result.business_requests_inflight, 1U);
            EXPECT_FALSE(json["upload_task_creation_enabled"].asBool());
            EXPECT_EQ(json["business_requests_inflight"].asUInt64(), 1U);
            EXPECT_EQ(counts.database, 1U);
            EXPECT_EQ(counts.redis, 1U);
            EXPECT_EQ(counts.staging_storage, 1U);
            EXPECT_EQ(counts.final_storage, 1U);
            EXPECT_EQ(counts.storage_jobs, 0U);
            EXPECT_TRUE(result.components.contains("redis"));
            EXPECT_FALSE(result.components.contains("storage_jobs"));

            runtime->ReleaseBusinessRequest();
            const auto drained = drogon::sync_wait(service.CheckReadiness());
            EXPECT_EQ(drained.business_requests_inflight, 0U);
        }

        TEST(HealthServiceTest, WorkerReadinessChecksQueueButNotRedis) {
            CheckCounts counts;
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::Worker,
                "worker-1"
            );
            runtime->SetWorkerAccepting(true);
            runtime->MarkInitialized();
            HealthService service(runtime, HealthyChecks(counts));

            const auto result = drogon::sync_wait(service.CheckReadiness());

            EXPECT_EQ(result.overall_status, "healthy");
            EXPECT_TRUE(result.worker_claiming_enabled);
            EXPECT_TRUE(result.worker_accepting);
            EXPECT_EQ(counts.database, 1U);
            EXPECT_EQ(counts.redis, 0U);
            EXPECT_EQ(counts.staging_storage, 1U);
            EXPECT_EQ(counts.final_storage, 1U);
            EXPECT_EQ(counts.storage_jobs, 1U);
            EXPECT_FALSE(result.components.contains("redis"));
            EXPECT_TRUE(result.components.contains("storage_jobs"));
        }

        TEST(HealthServiceTest, ObservationWorkerChecksDependenciesAndReportsMode) {
            CheckCounts counts;
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::Worker,
                "worker-observer-1",
                false
            );
            runtime->MarkInitialized();
            HealthService service(runtime, HealthyChecks(counts));

            const auto result = drogon::sync_wait(service.CheckReadiness());
            const auto json = result.ToJson();

            EXPECT_EQ(result.overall_status, "healthy");
            EXPECT_FALSE(result.worker_claiming_enabled);
            EXPECT_FALSE(result.worker_accepting);
            EXPECT_FALSE(json["worker_claiming_enabled"].asBool());
            EXPECT_FALSE(json["worker_accepting"].asBool());
            EXPECT_EQ(counts.database, 1U);
            EXPECT_EQ(counts.redis, 0U);
            EXPECT_EQ(counts.staging_storage, 1U);
            EXPECT_EQ(counts.final_storage, 1U);
            EXPECT_EQ(counts.storage_jobs, 1U);
        }

        TEST(HealthServiceTest, UnreadyRuntimeFailsFastWithoutDependencyChecks) {
            CheckCounts counts;
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::All,
                "all-1"
            );
            HealthService service(runtime, HealthyChecks(counts));

            auto result = drogon::sync_wait(service.CheckReadiness());
            EXPECT_EQ(result.overall_status, "unhealthy");
            EXPECT_EQ(
                result.components.at("runtime").message,
                "Process initialization is incomplete"
            );
            EXPECT_EQ(counts.database, 0U);

            runtime->SetWorkerAccepting(true);
            runtime->MarkInitialized();
            static_cast<void>(runtime->BeginDrain());
            result = drogon::sync_wait(service.CheckReadiness());
            EXPECT_EQ(result.components.at("runtime").message, "Process is draining");
            EXPECT_EQ(counts.database, 0U);
        }

        TEST(HealthServiceTest, SanitizesDependencyFailures) {
            CheckCounts counts;
            auto checks = HealthyChecks(counts);
            checks.database = [&counts](disk::utils::LogContext)
                -> drogon::Task<ComponentStatus> {
                counts.database++;
                throw std::runtime_error("postgres://user:password@secret-host/disk");
                co_return ComponentStatus{};
            };
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::Api,
                "api-1"
            );
            runtime->MarkInitialized();
            HealthService service(runtime, std::move(checks));

            const auto result = drogon::sync_wait(service.CheckReadiness());
            const auto json = result.ToJson().toStyledString();

            EXPECT_EQ(result.overall_status, "unhealthy");
            EXPECT_EQ(
                result.components.at("database").message,
                "Database check failed"
            );
            EXPECT_EQ(json.find("password"), std::string::npos);
            EXPECT_EQ(json.find("secret-host"), std::string::npos);
        }

        TEST(HealthServiceTest, NormalizesDependencyCheckResults) {
            CheckCounts counts;
            auto checks = HealthyChecks(counts);
            checks.database = {};
            checks.staging_storage = [](disk::utils::LogContext)
                -> drogon::Task<ComponentStatus> {
                co_return ComponentStatus{
                    .status = "unhealthy",
                    .message = "internal staging detail",
                    .latency_ms = 37,
                };
            };
            checks.final_storage = [](disk::utils::LogContext)
                -> drogon::Task<ComponentStatus> {
                co_return ComponentStatus{
                    .status = "healthy",
                    .message = "discard this detail",
                    .latency_ms = 11,
                };
            };
            auto runtime = std::make_shared<disk::runtime::ProcessRuntimeState>(
                disk::utils::ProcessRole::Api,
                "api-1"
            );
            runtime->MarkInitialized();
            HealthService service(runtime, std::move(checks));

            const auto result = drogon::sync_wait(service.CheckReadiness());

            EXPECT_EQ(result.overall_status, "unhealthy");
            EXPECT_EQ(result.components.at("database").status, "unhealthy");
            EXPECT_EQ(
                result.components.at("database").message,
                "Database check failed"
            );
            EXPECT_EQ(result.components.at("database").latency_ms, 0);
            EXPECT_EQ(
                result.components.at("staging_storage").message,
                "Staging storage check failed"
            );
            EXPECT_EQ(result.components.at("staging_storage").latency_ms, 37);
            EXPECT_TRUE(result.components.at("final_storage").message.empty());
            EXPECT_EQ(result.components.at("final_storage").latency_ms, 11);
        }
    } // namespace
} // namespace disk::health
