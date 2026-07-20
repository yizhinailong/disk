#include "services/HealthService.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace disk::health {
    namespace {
        struct CheckCounts {
            size_t database{ 0 };
            size_t redis{ 0 };
            size_t staging_storage{ 0 };
            size_t final_storage{ 0 };
            size_t storage_jobs{ 0 };
        };

        [[nodiscard]] auto HealthyCheck(size_t& calls) -> ComponentCheck {
            return [&calls]() -> drogon::Task<ComponentStatus> {
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
                "api-1"
            );
            runtime->MarkInitialized();
            HealthService service(runtime, HealthyChecks(counts));

            const auto result = drogon::sync_wait(service.CheckReadiness());

            EXPECT_EQ(result.overall_status, "healthy");
            EXPECT_FALSE(result.worker_claiming_enabled);
            EXPECT_FALSE(result.worker_accepting);
            EXPECT_EQ(counts.database, 1U);
            EXPECT_EQ(counts.redis, 1U);
            EXPECT_EQ(counts.staging_storage, 1U);
            EXPECT_EQ(counts.final_storage, 1U);
            EXPECT_EQ(counts.storage_jobs, 0U);
            EXPECT_TRUE(result.components.contains("redis"));
            EXPECT_FALSE(result.components.contains("storage_jobs"));
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
            checks.database = [&counts]() -> drogon::Task<ComponentStatus> {
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
    } // namespace
} // namespace disk::health
