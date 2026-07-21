/**
 * @file StorageJobWorker.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 持久存储任务 Worker
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <drogon/orm/DbClient.h>

#include "services/StorageJobRepository.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {
    class IBlobStore;
    class IMultipartUploadCleaner;
    class UploadStagingStorage;
} // namespace disk::storage

namespace disk::jobs {

    [[nodiscard]] constexpr auto EffectiveWorkerClaimBatchSize(
        size_t configured_batch_size,
        size_t concurrency
    ) noexcept -> size_t {
        return std::min(configured_batch_size, concurrency);
    }

    [[nodiscard]]
    auto BuildStorageJobLogContext(const StorageJob& job) -> disk::utils::LogContext;

    struct StorageJobWorkerOptions {
        size_t batch_size{ 16 };
        uint32_t lease_duration_seconds{ 120 };
        uint32_t retry_base_seconds{ 5 };
        uint32_t retry_cap_seconds{ 3600 };
    };

    struct JobExecutionResult {
        bool succeeded{ false };
        bool retryable{ false };
        bool outcome_persisted{ false };
        std::string error;
    };

    struct StorageJobRunResult {
        size_t claimed{ 0 };
        size_t succeeded{ 0 };
        size_t retried{ 0 };
        size_t dead_lettered{ 0 };
        size_t ownership_lost{ 0 };
    };

    class StorageJobWorker {
    public:
        StorageJobWorker(
            drogon::orm::DbClientPtr db_client,
            disk::storage::UploadStagingStorage* staging_storage,
            disk::storage::IBlobStore* blob_store,
            std::string instance_id,
            StorageJobWorkerOptions options = {},
            disk::storage::IMultipartUploadCleaner* multipart_upload_cleaner = nullptr
        );

        [[nodiscard]]
        auto RunOnce() const -> drogon::Task<Result<StorageJobRunResult>>;

        [[nodiscard]]
        auto ExecuteJob(const StorageJob& job) const -> drogon::Task<JobExecutionResult>;

        [[nodiscard]]
        static auto ComputeRetryDelaySeconds(
            uint64_t job_id,
            uint32_t attempts,
            uint32_t base_seconds,
            uint32_t cap_seconds
        ) -> uint32_t;

    private:
        using JobHandler = drogon::Task<JobExecutionResult> (StorageJobWorker::*)(
            const StorageJob&
        ) const;

        enum class PersistDisposition {
            Succeeded,
            Retried,
            DeadLettered,
            OwnershipLost,
        };

        [[nodiscard]]
        auto ProcessClaimedJob(const StorageJob& job) const -> drogon::Task<PersistDisposition>;

        [[nodiscard]]
        auto ExecuteStagingCleanup(const StorageJob& job) const
            -> drogon::Task<JobExecutionResult>;

        [[nodiscard]]
        auto ExecuteMultipartAbort(const StorageJob& job) const
            -> drogon::Task<JobExecutionResult>;

        [[nodiscard]]
        auto ExecuteBlobGc(const StorageJob& job) const -> drogon::Task<JobExecutionResult>;

        [[nodiscard]]
        auto ExecuteExpireUploads(const StorageJob& job) const
            -> drogon::Task<JobExecutionResult>;

        [[nodiscard]]
        auto ExecuteExpireTrash(const StorageJob& job) const
            -> drogon::Task<JobExecutionResult>;

        [[nodiscard]]
        auto ExecuteStorageReconcile(const StorageJob& job) const
            -> drogon::Task<JobExecutionResult>;

        drogon::orm::DbClientPtr m_db_client;
        disk::storage::UploadStagingStorage* m_staging_storage{};
        disk::storage::IBlobStore* m_blob_store{};
        disk::storage::IMultipartUploadCleaner* m_multipart_upload_cleaner{};
        std::string m_instance_id;
        StorageJobWorkerOptions m_options;
        std::unordered_map<std::string, JobHandler> m_handlers;
    };

} // namespace disk::jobs
