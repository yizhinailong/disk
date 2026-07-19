/**
 * @file StorageJobWorker.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 持久存储任务 Worker
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <drogon/orm/DbClient.h>

#include "services/StorageJobRepository.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::storage {
    class IBlobStore;
    class UploadStagingStorage;
} // namespace disk::storage

namespace disk::jobs {

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
            StorageJobWorkerOptions options = {}
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
        enum class PersistDisposition {
            Succeeded,
            Retried,
            DeadLettered,
            OwnershipLost,
        };

        [[nodiscard]]
        auto ProcessClaimedJob(const StorageJob& job) const -> drogon::Task<PersistDisposition>;

        [[nodiscard]]
        auto ExecuteBlobGc(const StorageJob& job) const -> drogon::Task<JobExecutionResult>;

        drogon::orm::DbClientPtr m_db_client;
        disk::storage::UploadStagingStorage* m_staging_storage{};
        disk::storage::IBlobStore* m_blob_store{};
        std::string m_instance_id;
        StorageJobWorkerOptions m_options;
    };

} // namespace disk::jobs
