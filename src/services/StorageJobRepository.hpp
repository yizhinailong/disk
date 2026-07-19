/**
 * @file StorageJobRepository.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief PostgreSQL 持久存储任务队列原语
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

namespace disk::jobs {

    inline constexpr std::string_view kStagingCleanupJobType = "staging_cleanup";
    inline constexpr std::string_view kMultipartAbortJobType = "multipart_abort";
    inline constexpr std::string_view kBlobGcJobType = "blob_gc";
    inline constexpr std::string_view kExpireUploadsJobType = "expire_uploads";
    inline constexpr std::string_view kStorageReconcileJobType = "storage_reconcile";
    inline constexpr size_t kDefaultExpireUploadsPageSize = 100;
    inline constexpr size_t kMaxExpireUploadsPageSize = 500;

    enum class StorageJobStatus : int16_t {
        Pending = 0,
        Running = 1,
        Retry = 2,
        Succeeded = 3,
        DeadLetter = 4,
    };

    [[nodiscard]] constexpr auto ToStorageValue(StorageJobStatus status) noexcept -> int16_t {
        return static_cast<int16_t>(status);
    }

    [[nodiscard]] constexpr auto ParseStorageJobStatus(int16_t value) noexcept
        -> std::optional<StorageJobStatus> {
        switch (value) {
            case ToStorageValue(StorageJobStatus::Pending):
                return StorageJobStatus::Pending;
            case ToStorageValue(StorageJobStatus::Running):
                return StorageJobStatus::Running;
            case ToStorageValue(StorageJobStatus::Retry):
                return StorageJobStatus::Retry;
            case ToStorageValue(StorageJobStatus::Succeeded):
                return StorageJobStatus::Succeeded;
            case ToStorageValue(StorageJobStatus::DeadLetter):
                return StorageJobStatus::DeadLetter;
            default:
                return std::nullopt;
        }
    }

    struct NewStorageJob {
        std::string job_type;
        std::string aggregate_id;
        std::string dedupe_key;
        Json::Value payload{ Json::objectValue };
        uint32_t max_attempts{ 8 };
    };

    struct StorageJob {
        uint64_t id{ 0 };
        std::string job_type;
        std::string aggregate_id;
        std::string dedupe_key;
        Json::Value payload{ Json::objectValue };
        StorageJobStatus status{ StorageJobStatus::Pending };
        uint32_t attempts{ 0 };
        uint32_t max_attempts{ 0 };
        std::string locked_by;
    };

    enum class BlobGcReferenceGate {
        Allowed,
        Blocked,
    };

    class StorageJobRepository {
    public:
        explicit StorageJobRepository(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto Enqueue(const NewStorageJob& job) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto Enqueue(
            const drogon::orm::DbClientPtr& client,
            const NewStorageJob& job
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto EnqueueOrRearmSucceeded(
            const drogon::orm::DbClientPtr& client,
            const NewStorageJob& job
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto CheckBlobGcReferenceGate(
            const drogon::orm::DbClientPtr& client,
            uint64_t content_id
        ) const -> drogon::Task<BlobGcReferenceGate>;

        [[nodiscard]]
        auto ClaimReadyBatch(
            const std::string& instance_id,
            size_t limit,
            uint32_t lease_duration_seconds
        ) const -> drogon::Task<std::vector<StorageJob>>;

        [[nodiscard]]
        auto RenewLease(
            uint64_t job_id,
            const std::string& instance_id,
            uint32_t lease_duration_seconds
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkSucceeded(uint64_t job_id, const std::string& instance_id) const
            -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkSucceeded(
            const drogon::orm::DbClientPtr& client,
            uint64_t job_id,
            const std::string& instance_id
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkFailed(
            uint64_t job_id,
            const std::string& instance_id,
            const std::string& error,
            bool retryable,
            uint32_t retry_delay_seconds
        ) const -> drogon::Task<std::optional<StorageJobStatus>>;

        [[nodiscard]]
        auto ReplayDeadLetter(uint64_t job_id) const -> drogon::Task<bool>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} // namespace disk::jobs
