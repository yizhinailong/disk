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

namespace trantor {
    class ConcurrentTaskQueue;
}

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

    enum class UploadCompleteStage : uint8_t {
        ClaimLease,
        LoadMetadata,
        Assemble,
        DedupLookup,
        Promote,
        Commit,
        Count,
    };

    enum class Dependency : uint8_t {
        PostgreSql,
        Redis,
        S3,
        Count,
    };

    enum class DependencyOutcome : uint8_t {
        Success,
        Timeout,
        Connection,
        Conflict,
        NotFound,
        Retryable,
        Permanent,
        Protocol,
        Other,
        Count,
    };

    enum class ThreadQueue : uint8_t {
        LocalFile,
        LocalAssembly,
        LocalBlob,
        S3,
        Count,
    };

    inline constexpr size_t kHttpOperationCount = static_cast<size_t>(HttpOperation::Count);
    inline constexpr size_t kHttpStatusClassCount = static_cast<size_t>(HttpStatusClass::Count);
    inline constexpr size_t kStorageJobOutcomeCount = static_cast<size_t>(StorageJobOutcome::Count);
    inline constexpr size_t kUploadCompleteStageCount =
        static_cast<size_t>(UploadCompleteStage::Count);
    inline constexpr size_t kDependencyCount = static_cast<size_t>(Dependency::Count);
    inline constexpr size_t kDependencyOutcomeCount =
        static_cast<size_t>(DependencyOutcome::Count);
    inline constexpr size_t kThreadQueueCount = static_cast<size_t>(ThreadQueue::Count);
    inline constexpr size_t kStorageJobTypeCount = 7;
    inline constexpr size_t kReconciliationFindingTypeCount = 11;
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
    inline constexpr std::array<double, 14> kUploadCompleteDurationBucketsSeconds{
        0.005,
        0.01,
        0.025,
        0.05,
        0.1,
        0.25,
        1.0,
        5.0,
        15.0,
        30.0,
        60.0,
        120.0,
        300.0,
        600.0,
    };

    struct MetricsSnapshot final {
        std::array<std::array<uint64_t, kHttpStatusClassCount>, kHttpOperationCount>
            http_requests{};
        std::array<std::array<uint64_t, kDurationBucketsSeconds.size()>, kHttpOperationCount>
            http_duration_buckets{};
        std::array<uint64_t, kHttpOperationCount> http_duration_count{};
        std::array<uint64_t, kHttpOperationCount> http_duration_microseconds{};
        uint64_t upload_chunks_total{ 0 };
        uint64_t upload_chunk_bytes_total{ 0 };
        std::array<
            std::array<uint64_t, kUploadCompleteDurationBucketsSeconds.size()>,
            kUploadCompleteStageCount>
            upload_complete_duration_buckets{};
        std::array<uint64_t, kUploadCompleteStageCount> upload_complete_duration_count{};
        std::array<uint64_t, kUploadCompleteStageCount> upload_complete_duration_microseconds{};
        std::array<std::array<uint64_t, kStorageJobOutcomeCount>, kStorageJobTypeCount>
            storage_job_runs{};
        std::array<std::array<uint64_t, kDurationBucketsSeconds.size()>, kStorageJobTypeCount>
            storage_job_duration_buckets{};
        std::array<uint64_t, kStorageJobTypeCount> storage_job_duration_count{};
        std::array<uint64_t, kStorageJobTypeCount> storage_job_duration_microseconds{};
        std::array<uint64_t, kStorageJobTypeCount> storage_job_takeovers{};
        std::array<std::array<uint64_t, kDependencyOutcomeCount>, kDependencyCount>
            dependency_calls{};
        std::array<std::array<uint64_t, kDurationBucketsSeconds.size()>, kDependencyCount>
            dependency_duration_buckets{};
        std::array<uint64_t, kDependencyCount> dependency_duration_count{};
        std::array<uint64_t, kDependencyCount> dependency_duration_microseconds{};
        std::array<uint64_t, kDependencyCount> dependency_calls_inflight{};
        std::array<uint64_t, kDependencyCount> dependency_pool_demand{};
        std::array<uint64_t, kDependencyCount> dependency_pool_leases{};
        std::array<uint64_t, kDependencyCount> dependency_pool_capacity{};
        std::array<uint64_t, kThreadQueueCount> thread_queue_depth{};
        std::array<uint64_t, kThreadQueueCount> thread_queue_workers{};
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
    [[nodiscard]] auto UploadCompleteStageName(UploadCompleteStage stage) noexcept
        -> std::string_view;
    [[nodiscard]] auto StorageJobOutcomeName(StorageJobOutcome outcome) noexcept -> std::string_view;
    [[nodiscard]] auto DependencyName(Dependency dependency) noexcept -> std::string_view;
    [[nodiscard]] auto DependencyOutcomeName(DependencyOutcome outcome) noexcept -> std::string_view;
    [[nodiscard]] auto ThreadQueueName(ThreadQueue queue) noexcept -> std::string_view;
    [[nodiscard]] auto ReconciliationFindingTypeIndex(std::string_view finding_type) noexcept
        -> size_t;

    class DependencyCallTimer final {
    public:
        explicit DependencyCallTimer(Dependency dependency, bool uses_pool = true);
        ~DependencyCallTimer();

        DependencyCallTimer(const DependencyCallTimer&) = delete;
        auto operator=(const DependencyCallTimer&) -> DependencyCallTimer& = delete;
        DependencyCallTimer(DependencyCallTimer&&) = delete;
        auto operator=(DependencyCallTimer&&) -> DependencyCallTimer& = delete;

        auto Finish(DependencyOutcome outcome) noexcept -> void;

    private:
        Dependency m_dependency;
        bool m_uses_pool;
        bool m_finished{ false };
        std::chrono::steady_clock::time_point m_started_at;
    };

    class MetricsRegistry final {
    public:
        [[nodiscard]] static auto GetInstance() -> MetricsRegistry&;

        auto RecordHttpRequest(
            HttpOperation operation,
            int status_code,
            std::chrono::microseconds duration
        ) -> void;

        auto RecordUploadChunk(uint64_t size_bytes) -> void;

        auto RecordUploadCompleteStage(
            UploadCompleteStage stage,
            std::chrono::microseconds duration
        ) -> void;

        auto RecordStorageJob(
            std::string_view job_type,
            StorageJobOutcome outcome,
            std::chrono::microseconds duration,
            bool lease_takeover
        ) -> void;

        auto BeginDependencyCall(Dependency dependency, bool uses_pool = true) -> void;

        auto RecordDependencyCall(
            Dependency dependency,
            DependencyOutcome outcome,
            std::chrono::microseconds duration,
            bool uses_pool = true
        ) -> void;

        auto AcquireDependencyPoolLease(Dependency dependency) -> void;
        auto ReleaseDependencyPoolLease(Dependency dependency) -> void;
        auto SetDependencyPoolCapacity(Dependency dependency, uint64_t capacity) -> void;

        auto RegisterThreadQueue(
            ThreadQueue queue,
            const std::shared_ptr<trantor::ConcurrentTaskQueue>& task_queue,
            uint64_t workers
        ) -> void;

        [[nodiscard]] auto Snapshot() const -> MetricsSnapshot;

    private:
        MetricsRegistry() = default;

        mutable std::mutex m_mutex;
        MetricsSnapshot m_snapshot;
        std::array<std::weak_ptr<trantor::ConcurrentTaskQueue>, kThreadQueueCount>
            m_thread_queues;
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
