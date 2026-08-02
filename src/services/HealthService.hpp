/**
 * @file HealthService.hpp
 * @brief 角色感知的 liveness/readiness 服务
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include "services/ProcessRuntime.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {
    class IBlobStore;
    class UploadStagingStorage;
} // namespace disk::storage

namespace disk::health {

    struct ComponentStatus {
        std::string status;
        std::string message;
        int64_t latency_ms{ 0 };

        [[nodiscard]]
        auto ToJson() const -> Json::Value;
    };

    struct HealthResult {
        std::string overall_status;
        std::string role;
        std::string instance_id;
        bool initialized{ false };
        bool draining{ false };
        bool worker_claiming_enabled{ false };
        bool worker_accepting{ false };
        bool upload_task_creation_enabled{ true };
        uint64_t business_requests_inflight{ 0 };
        std::string version;
        int64_t uptime{ 0 };
        std::string timestamp;
        int64_t total_check_ms{ 0 };
        std::map<std::string, ComponentStatus> components;

        [[nodiscard]]
        auto ToJson() const -> Json::Value;
    };

    using ComponentCheck =
        std::function<drogon::Task<ComponentStatus>(disk::utils::LogContext)>;

    struct HealthCheckCallbacks {
        ComponentCheck database;
        ComponentCheck redis;
        ComponentCheck staging_storage;
        ComponentCheck final_storage;
        ComponentCheck storage_jobs;
    };

    class HealthService final {
    public:
        HealthService(
            drogon::orm::DbClientPtr db_client,
            drogon::nosql::RedisClientPtr redis_client,
            disk::storage::UploadStagingStorage* staging_storage,
            disk::storage::IBlobStore* blob_store,
            std::shared_ptr<disk::runtime::ProcessRuntimeState> runtime_state
        );

        HealthService(
            std::shared_ptr<disk::runtime::ProcessRuntimeState> runtime_state,
            HealthCheckCallbacks checks
        );

        [[nodiscard]]
        auto CheckLiveness() const -> HealthResult;

        [[nodiscard]]
        auto CheckReadiness(disk::utils::LogContext log_context = {}) const
            -> drogon::Task<HealthResult>;

    private:
        [[nodiscard]]
        auto BuildBaseResult() const -> HealthResult;

        std::shared_ptr<disk::runtime::ProcessRuntimeState> m_runtime_state;
        HealthCheckCallbacks m_checks;
        std::chrono::steady_clock::time_point m_start_time;
    };

} // namespace disk::health
