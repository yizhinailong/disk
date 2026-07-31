/**
 * @file UploadLifecycleService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传生命周期领域服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "services/UploadStateMachine.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {
    class IBlobStore;
    class UploadStagingStorage;
} // namespace disk::storage

namespace disk::upload {

    enum class InitDecisionType {
        InstantUpload,
        ResumeUpload,
        StartNewUpload,
    };

    struct InitDecision {
        InitDecisionType type{ InitDecisionType::StartNewUpload };
        std::string upload_id;
    };

    struct ChunkAcceptanceError {
        ErrorInfo error{ ErrorCode::ValidationFailed };
        uint64_t expected_size{ 0 };
    };

    struct ChunkAcceptance {
        uint64_t expected_size{ 0 };
    };

    enum class FinalizeStorageDecisionType {
        ReuseExistingContent,
        PromoteAsNewContent,
    };

    struct FinalizeStorageDecision {
        FinalizeStorageDecisionType type{ FinalizeStorageDecisionType::PromoteAsNewContent };
        std::optional<uint64_t> existing_content_id;
    };

    struct UploadCacheInvalidation {
        std::vector<std::string> upload_task_ids;
        std::vector<uint64_t> file_list_folder_ids;
    };

    struct UploadExpirationBatchResult {
        size_t candidates{ 0 };
        size_t expired{ 0 };
    };

    [[nodiscard]] constexpr auto ShouldContinueExpirationScan(
        size_t candidates,
        size_t limit
    ) noexcept -> bool {
        return limit > 0 && candidates == limit;
    }

    struct LifecycleFileItem {
        uint64_t id{ 0 };
        std::string name;
        uint64_t size{ 0 };
        std::string hash;
        std::string mime_type;
        uint64_t parent_id{ 0 };
        std::string created_at;
    };

    struct InitUploadCommand {
        std::string filename;
        uint64_t file_size{ 0 };
        std::string file_hash;
        uint64_t parent_id{ 0 };
        uint64_t user_id{ 0 };
        uint64_t max_file_size{ 0 };
        uint64_t chunk_size{ 0 };
        int64_t expiry_seconds{ 0 };
        bool upload_task_creation_enabled{ true };
    };

    struct InitUploadOutcome {
        std::string upload_id;
        uint32_t chunk_size{ 0 };
        uint32_t total_chunks{ 0 };
        std::vector<uint32_t> uploaded_chunks;
        bool instant_upload{ false };
        std::optional<LifecycleFileItem> file;
        UploadCacheInvalidation invalidation;
    };

    struct CompleteUploadCommand {
        std::string upload_id;
        uint64_t user_id{ 0 };
        std::string lease_owner;
        uint32_t lease_duration_seconds{ 0 };
    };

    struct CompleteUploadOutcome {
        std::optional<LifecycleFileItem> file;
        UploadCacheInvalidation invalidation;
    };

    [[nodiscard]] auto DecideInitFlow(
        bool has_existing_content,
        std::string_view existing_task_id
    ) -> InitDecision;

    [[nodiscard]] auto ValidateChunkAcceptance(
        uint32_t chunk_index,
        uint64_t data_size,
        uint64_t file_size,
        uint32_t chunk_size,
        uint32_t total_chunks
    ) -> std::expected<ChunkAcceptance, ChunkAcceptanceError>;

    [[nodiscard]] auto DecideFinalizeStorage(std::optional<uint64_t> existing_content_id)
        -> FinalizeStorageDecision;

    class UploadLifecycleService {
    public:
        explicit UploadLifecycleService(
            drogon::orm::DbClientPtr db_client,
            disk::storage::UploadStagingStorage* upload_staging_storage,
            disk::storage::IBlobStore* blob_store
        );

        [[nodiscard]]
        auto InitializeUpload(
            InitUploadCommand command,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<Result<InitUploadOutcome>>;

        [[nodiscard]]
        auto CompleteUpload(
            CompleteUploadCommand command,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<Result<CompleteUploadOutcome>>;

        [[nodiscard]]
        auto CancelInProgressUpload(
            const std::string& upload_id,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<uint64_t>>;

        [[nodiscard]]
        auto ExpireInProgressUploads(
            size_t limit,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<Result<UploadExpirationBatchResult>>;

    private:
        [[nodiscard]]
        auto ExpireInProgressUpload(
            const std::string& upload_id,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<Result<bool>>;

        drogon::orm::DbClientPtr m_db_client;
        disk::storage::UploadStagingStorage* m_upload_staging_storage{};
        disk::storage::IBlobStore* m_blob_store{};
    };

} // namespace disk::upload
