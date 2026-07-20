/**
 * @file HealthService.cpp
 * @brief 角色感知的 liveness/readiness 服务实现
 */

#include "services/HealthService.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "services/RedisService.hpp"
#include "storage/IBlobStore.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/LogHelper.hpp"

namespace disk::health {
    namespace {
        [[nodiscard]] auto ElapsedMilliseconds(
            std::chrono::steady_clock::time_point start
        ) noexcept -> int64_t {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start
            )
                .count();
        }

        [[nodiscard]] auto HealthyStatus(
            std::chrono::steady_clock::time_point start
        ) -> ComponentStatus {
            return ComponentStatus{
                .status = "healthy",
                .latency_ms = ElapsedMilliseconds(start),
            };
        }

        [[nodiscard]] auto UnhealthyStatus(
            std::chrono::steady_clock::time_point start
        ) -> ComponentStatus {
            return ComponentStatus{
                .status = "unhealthy",
                .latency_ms = ElapsedMilliseconds(start),
            };
        }

        auto CheckDatabase(drogon::orm::DbClientPtr db_client)
            -> drogon::Task<ComponentStatus> {
            const auto start = std::chrono::steady_clock::now();
            if (db_client == nullptr) {
                co_return UnhealthyStatus(start);
            }
            try {
                static_cast<void>(co_await db_client->execSqlCoro("SELECT 1"));
                co_return HealthyStatus(start);
            } catch (const std::exception&) {
                co_return UnhealthyStatus(start);
            }
        }

        auto CheckRedis(drogon::nosql::RedisClientPtr redis_client)
            -> drogon::Task<ComponentStatus> {
            const auto start = std::chrono::steady_clock::now();
            if (redis_client == nullptr) {
                co_return UnhealthyStatus(start);
            }
            disk::services::RedisService::Initialize(std::move(redis_client));
            const auto result = co_await disk::services::RedisService::GetInstance()->Ping();
            co_return result.has_value() && *result ? HealthyStatus(start) : UnhealthyStatus(start);
        }

        auto CheckStagingStorage(disk::storage::UploadStagingStorage* storage)
            -> drogon::Task<ComponentStatus> {
            const auto start = std::chrono::steady_clock::now();
            if (storage == nullptr) {
                co_return UnhealthyStatus(start);
            }
            try {
                auto result = co_await storage->ListStagingObjects({}, 1);
                co_return result.has_value() ? HealthyStatus(start) : UnhealthyStatus(start);
            } catch (const std::exception&) {
                co_return UnhealthyStatus(start);
            }
        }

        auto CheckFinalStorage(disk::storage::IBlobStore* storage)
            -> drogon::Task<ComponentStatus> {
            const auto start = std::chrono::steady_clock::now();
            if (storage == nullptr) {
                co_return UnhealthyStatus(start);
            }
            try {
                auto result = co_await storage->ListFinalObjects({}, 1);
                co_return result.has_value() ? HealthyStatus(start) : UnhealthyStatus(start);
            } catch (const std::exception&) {
                co_return UnhealthyStatus(start);
            }
        }

        auto CheckStorageJobs(drogon::orm::DbClientPtr db_client)
            -> drogon::Task<ComponentStatus> {
            const auto start = std::chrono::steady_clock::now();
            if (db_client == nullptr) {
                co_return UnhealthyStatus(start);
            }
            try {
                static_cast<void>(co_await db_client->execSqlCoro(
                    "SELECT 1 FROM storage_jobs LIMIT 1"
                ));
                co_return HealthyStatus(start);
            } catch (const std::exception&) {
                co_return UnhealthyStatus(start);
            }
        }

        [[nodiscard]] auto RuntimeFailureMessage(
            const disk::runtime::ProcessRuntimeState& state
        ) -> std::string {
            if (!state.IsInitialized()) {
                return "Process initialization is incomplete";
            }
            if (state.IsDraining()) {
                return "Process is draining";
            }
            return "Worker is not accepting storage jobs";
        }
    } // namespace

    auto ComponentStatus::ToJson() const -> Json::Value {
        Json::Value json(Json::objectValue);
        json["status"] = status;
        if (!message.empty()) {
            json["message"] = message;
        }
        json["latency_ms"] = static_cast<Json::Int64>(latency_ms);
        return json;
    }

    auto HealthResult::ToJson() const -> Json::Value {
        Json::Value json(Json::objectValue);
        json["overall_status"] = overall_status;
        json["role"] = role;
        json["instance_id"] = instance_id;
        json["initialized"] = initialized;
        json["draining"] = draining;
        json["version"] = version;
        json["uptime"] = static_cast<Json::Int64>(uptime);
        json["total_check_ms"] = static_cast<Json::Int64>(total_check_ms);
        json["timestamp"] = timestamp;

        Json::Value components_json(Json::objectValue);
        for (const auto& [name, status] : components) {
            components_json[name] = status.ToJson();
        }
        json["components"] = std::move(components_json);
        return json;
    }

    HealthService::HealthService(
        drogon::orm::DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client,
        disk::storage::UploadStagingStorage* staging_storage,
        disk::storage::IBlobStore* blob_store,
        std::shared_ptr<disk::runtime::ProcessRuntimeState> runtime_state
    ) : HealthService(std::move(runtime_state), HealthCheckCallbacks{
                                                    .database = [db_client]() { return CheckDatabase(db_client); },
                                                    .redis = [redis_client]() { return CheckRedis(redis_client); },
                                                    .staging_storage = [staging_storage]() { return CheckStagingStorage(staging_storage); },
                                                    .final_storage = [blob_store]() { return CheckFinalStorage(blob_store); },
                                                    .storage_jobs = [db_client]() { return CheckStorageJobs(db_client); },
                                                }) {
    }

    HealthService::HealthService(
        std::shared_ptr<disk::runtime::ProcessRuntimeState> runtime_state,
        HealthCheckCallbacks checks
    ) : m_runtime_state(std::move(runtime_state)),
        m_checks(std::move(checks)),
        m_start_time(std::chrono::steady_clock::now()) {
        if (m_runtime_state == nullptr) {
            throw std::invalid_argument("Health service runtime state is required");
        }
    }

    auto HealthService::CheckLiveness() const -> HealthResult {
        auto result = BuildBaseResult();
        result.overall_status = "healthy";
        result.components["runtime"] = ComponentStatus{
            .status = "healthy",
        };
        return result;
    }

    auto HealthService::CheckReadiness() const -> drogon::Task<HealthResult> {
        auto result = BuildBaseResult();
        const auto check_start = std::chrono::steady_clock::now();

        if (!m_runtime_state->IsReady()) {
            result.overall_status = "unhealthy";
            result.components["runtime"] = ComponentStatus{
                .status = "unhealthy",
                .message = RuntimeFailureMessage(*m_runtime_state),
            };
            co_return result;
        }
        result.components["runtime"] = ComponentStatus{ .status = "healthy" };

        result.components["database"] = co_await RunComponentCheck(
            "database",
            "Database check failed",
            m_checks.database
        );
        result.components["staging_storage"] = co_await RunComponentCheck(
            "staging_storage",
            "Staging storage check failed",
            m_checks.staging_storage
        );
        result.components["final_storage"] = co_await RunComponentCheck(
            "final_storage",
            "Final storage check failed",
            m_checks.final_storage
        );

        const auto role = m_runtime_state->Role();
        if (disk::utils::IncludesApi(role)) {
            result.components["redis"] = co_await RunComponentCheck(
                "redis",
                "Redis check failed",
                m_checks.redis
            );
        }
        if (disk::utils::IncludesWorker(role)) {
            result.components["storage_jobs"] = co_await RunComponentCheck(
                "storage_jobs",
                "Storage job queue check failed",
                m_checks.storage_jobs
            );
        }

        result.overall_status = "healthy";
        for (const auto& [name, status] : result.components) {
            static_cast<void>(name);
            if (status.status != "healthy") {
                result.overall_status = "unhealthy";
                break;
            }
        }
        result.total_check_ms = ElapsedMilliseconds(check_start);
        co_return result;
    }

    auto HealthService::BuildBaseResult() const -> HealthResult {
        const auto now = std::chrono::steady_clock::now();
        return HealthResult{
            .role = std::string(disk::utils::ProcessRoleName(m_runtime_state->Role())),
            .instance_id = m_runtime_state->InstanceId(),
            .initialized = m_runtime_state->IsInitialized(),
            .draining = m_runtime_state->IsDraining(),
            .version = "1.0.0",
            .uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_start_time).count(),
            .timestamp = GetTimestamp(),
        };
    }

    auto HealthService::RunComponentCheck(
        std::string component,
        std::string failure_message,
        const ComponentCheck& check
    ) const -> drogon::Task<ComponentStatus> {
        ComponentStatus status;
        try {
            if (!check) {
                throw std::runtime_error("Component check is not configured");
            }
            status = co_await check();
        } catch (const std::exception&) {
            status.status = "unhealthy";
            status.latency_ms = 0;
        }

        if (status.status != "healthy") {
            Logger::Warn() << "Health dependency check failed: component=" << component;
            status.status = "unhealthy";
            status.message = std::move(failure_message);
        } else {
            status.message.clear();
        }
        co_return status;
    }

    auto HealthService::GetTimestamp() -> std::string {
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &timestamp);
#else
        gmtime_r(&timestamp, &utc);
#endif

        std::ostringstream stream;
        stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return stream.str();
    }

} // namespace disk::health
