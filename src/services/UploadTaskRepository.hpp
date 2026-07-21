/**
 * @file UploadTaskRepository.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传任务持久化原语
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "models/UploadTasks.hpp"
#include "services/UploadLifecycleService.hpp"
#include "storage/UploadStagingStorage.hpp"

namespace disk::file {

    struct UploadTaskCleanupRecord {
        std::string id;
        std::string temp_path;
        uint64_t user_id{ 0 };
        uint64_t reserved_bytes{ 0 };
        disk::storage::UploadStagingSession staging_session;
    };

    struct CancelledUploadTaskRecord {
        UploadTaskCleanupRecord cleanup;
        uint64_t state_version{ 0 };
    };

    struct UploadTaskCancellationState {
        int status{ 0 };
        uint64_t state_version{ 0 };
        bool task_expired{ false };
    };

    using ExpiredUploadTaskRecord = UploadTaskCleanupRecord;

    enum class FinalizeClaimDisposition {
        Acquired,
        IncompleteChunks,
        LeaseHeld,
        CompletedReplay,
        Terminal,
        NotFound,
    };

    struct FinalizeClaimResult {
        FinalizeClaimDisposition disposition{ FinalizeClaimDisposition::NotFound };
        uint64_t state_version{ 0 };
        uint32_t finalize_attempts{ 0 };
        std::optional<uint64_t> completed_file_id;
    };

    enum class ChunkRecordDisposition {
        Accepted,
        TaskRejected,
        MetadataConflict,
    };

    /**
     * @brief 上传任务持久化原语
     */
    class UploadTaskRepository {
    public:
        explicit UploadTaskRepository(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto FindById(const std::string& upload_id) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        [[nodiscard]]
        auto FindByIdForUser(const std::string& upload_id, uint64_t user_id) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        [[nodiscard]]
        auto FindUnexpiredByIdForUser(const std::string& upload_id, uint64_t user_id) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        [[nodiscard]]
        auto FindInProgressByUserAndHash(uint64_t user_id, const std::string& file_hash) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        [[nodiscard]]
        auto FindInProgressIdByUserAndHash(uint64_t user_id, const std::string& file_hash) const
            -> drogon::Task<std::optional<std::string>>;

        [[nodiscard]]
        auto Create(
            drogon_model::disk::UploadTasks task,
            const disk::storage::UploadStagingSession& staging_session,
            uint32_t expiry_seconds
        ) const
            -> drogon::Task<drogon_model::disk::UploadTasks>;

        [[nodiscard]]
        auto FindStagingSessionForUser(const std::string& upload_id, uint64_t user_id) const
            -> drogon::Task<std::optional<disk::storage::UploadStagingSession>>;

        [[nodiscard]]
        auto ClaimFinalizeLease(
            const std::string& upload_id,
            uint64_t user_id,
            const std::string& lease_owner,
            uint32_t lease_duration_seconds
        ) const -> drogon::Task<FinalizeClaimResult>;

        [[nodiscard]]
        auto RenewFinalizeLease(
            const std::string& upload_id,
            uint64_t user_id,
            const std::string& lease_owner,
            uint64_t expected_state_version,
            uint32_t lease_duration_seconds
        ) const -> drogon::Task<std::optional<uint64_t>>;

        [[nodiscard]]
        auto RenewFinalizeLease(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id,
            uint64_t user_id,
            const std::string& lease_owner,
            uint64_t expected_state_version,
            uint32_t lease_duration_seconds
        ) const -> drogon::Task<std::optional<uint64_t>>;

        [[nodiscard]]
        auto MarkCompletedIfLeaseOwned(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id,
            uint64_t user_id,
            const std::string& lease_owner,
            uint64_t expected_state_version,
            uint64_t completed_file_id
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkFailedIfLeaseOwned(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id,
            uint64_t user_id,
            const std::string& lease_owner,
            uint64_t expected_state_version,
            int error_code,
            const std::string& fail_reason
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto RecordFinalizeErrorIfLeaseOwned(
            const std::string& upload_id,
            uint64_t user_id,
            const std::string& lease_owner,
            uint64_t expected_state_version,
            int error_code
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto DeleteInProgressById(const std::string& upload_id) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkCompleted(std::string const& upload_id) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkCompletedIfInProgress(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkCancelledIfInProgressReturning(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id,
            uint64_t user_id,
            const std::string& fail_reason
        ) const -> drogon::Task<std::optional<CancelledUploadTaskRecord>>;

        [[nodiscard]]
        auto FindCancellationStateByIdForUser(
            const std::string& upload_id,
            uint64_t user_id
        ) const -> drogon::Task<std::optional<UploadTaskCancellationState>>;

        [[nodiscard]]
        auto MarkExpiredIfInProgressBatch(
            const std::vector<std::string>& upload_ids,
            const std::string& fail_reason
        ) const -> drogon::Task<uint64_t>;

        [[nodiscard]]
        auto MarkExpiredIfInProgressReturning(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id,
            const std::string& fail_reason
        ) const -> drogon::Task<std::optional<ExpiredUploadTaskRecord>>;

        [[nodiscard]]
        auto RecordChunkIfInProgress(
            const std::string& upload_id,
            uint64_t user_id,
            uint32_t chunk_index,
            uint64_t size_bytes,
            const std::string& hash_md5,
            const std::string& object_key,
            const std::string& etag
        ) const -> drogon::Task<ChunkRecordDisposition>;

        [[nodiscard]]
        auto ListChunksForAssembly(const std::string& upload_id) const
            -> drogon::Task<std::vector<disk::storage::UploadStagingChunk>>;

        [[nodiscard]]
        auto ListUploadedChunkIndices(const std::string& upload_id) const
            -> drogon::Task<std::vector<uint32_t>>;

        [[nodiscard]]
        auto GetChunkCoverage(const std::string& upload_id) const
            -> drogon::Task<disk::upload::ChunkCoverage>;

        auto DeleteChunks(std::string const& upload_id) const -> drogon::Task<void>;

        auto DeleteChunks(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id
        ) const -> drogon::Task<void>;

        [[nodiscard]]
        auto FindExpiredInProgressBatch(size_t limit) const
            -> drogon::Task<std::vector<ExpiredUploadTaskRecord>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} // namespace disk::file
