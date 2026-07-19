/**
 * @file MetricsService.hpp
 * @brief Low-cardinality operational metrics for API and Worker processes
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>

#include "services/ProcessRuntime.hpp"

namespace disk::metrics {

    enum class HttpOperation : uint8_t {
        Health,
        Metrics,
        Auth,
        UploadInit,
        UploadChunk,
        UploadComplete,
        UploadCancel,
        Download,
        Share,
        Admin,
        Other,
        Count,
    };

    enum class HttpStatusClass : uint8_t {
        Informational,
        Success,
        Redirect,
        ClientError,
        ServerError,
        Other,
        Count,
    };

    enum class StorageJobOutcome : uint8_t {
        Succeeded,
        Retry,
        DeadLetter,
        OwnershipLost,
        Count,
    };

    inline constexpr size_t kHttpOperationCount = static_cast<size_t>(HttpOperation::Count);
    inline constexpr size_t kHttpStatusClassCount = static_cast<size_t>(HttpStatusClass::Count);
    inline constexpr size_t kStorageJobOutcomeCount = static_cast<size_t>(StorageJobOutcome::Count);
    inline constexpr size_t kStorageJobTypeCount = 7;
    inline constexpr size_t kReconciliationFindingTypeCount = 10;
    inline constexpr std::array<double, 8> kDurationBucketsSeconds{
        0.005,
        0.01,
        0.025,
        0.05,
        0.1,
        0.25,
        1.0,
        5.0,
    };

    struct MetricsSnapshot final {
        std::array<std::array<uint64_t, kHttpStatusClassCount>, kHttpOperationCount>
            http_requests{};
        std::array<std::array<uint64_t, kDurationBucketsSeconds.size()>, kHttpOperationCount>
            http_duration_buckets{};
        std::array<uint64_t, kHttpOperationCount> http_duration_count{};
        std::array<uint64_t, kHttpOperationCount> http_duration_microseconds{};
        std::array<std::array<uint64_t, kStorageJobOutcomeCount>, kStorageJobTypeCount>
            storage_job_runs{};
        std::array<std::array<uint64_t, kDurationBucketsSeconds.size()>, kStorageJobTypeCount>
            storage_job_duration_buckets{};
        std::array<uint64_t, kStorageJobTypeCount> storage_job_duration_count{};
        std::array<uint64_t, kStorageJobTypeCount> storage_job_duration_microseconds{};
        std::array<uint64_t, kStorageJobTypeCount> storage_job_takeovers{};
    };

    struct DatabaseMetricsSnapshot final {
        std::array<uint64_t, 5> storage_jobs{};
        std::array<uint64_t, 6> upload_tasks{};
        double oldest_ready_job_age_seconds{ 0.0 };
        uint64_t expired_job_leases{ 0 };
        std::array<uint64_t, kReconciliationFindingTypeCount> reconciliation_findings{};
        bool success{ false };
    };

    [[nodiscard]] auto ClassifyHttpOperation(std::string_view path) noexcept -> HttpOperation;
    [[nodiscard]] auto HttpOperationName(HttpOperation operation) noexcept -> std::string_view;
    [[nodiscard]] auto StorageJobOutcomeName(StorageJobOutcome outcome) noexcept -> std::string_view;
    [[nodiscard]] auto ReconciliationFindingTypeIndex(std::string_view finding_type) noexcept
        -> size_t;

    class MetricsRegistry final {
    public:
        [[nodiscard]] static auto GetInstance() -> MetricsRegistry&;

        auto RecordHttpRequest(
            HttpOperation operation,
            int status_code,
            std::chrono::microseconds duration
        ) -> void;

        auto RecordStorageJob(
            std::string_view job_type,
            StorageJobOutcome outcome,
            std::chrono::microseconds duration,
            bool lease_takeover
        ) -> void;

        [[nodiscard]] auto Snapshot() const -> MetricsSnapshot;

    private:
        MetricsRegistry() = default;

        mutable std::mutex m_mutex;
        MetricsSnapshot m_snapshot;
    };

    class MetricsService final {
    public:
        MetricsService(
            drogon::orm::DbClientPtr db_client,
            std::shared_ptr<disk::runtime::ProcessRuntimeState> runtime_state
        );

        [[nodiscard]] auto Render() const -> drogon::Task<std::string>;

        [[nodiscard]] static auto RenderSnapshot(
            const MetricsSnapshot& metrics,
            const DatabaseMetricsSnapshot& database,
            const disk::runtime::ProcessRuntimeState& runtime_state
        ) -> std::string;

    private:
        drogon::orm::DbClientPtr m_db_client;
        std::shared_ptr<disk::runtime::ProcessRuntimeState> m_runtime_state;
    };

} // namespace disk::metrics
