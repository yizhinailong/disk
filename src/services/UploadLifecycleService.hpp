/**
 * @file UploadLifecycleService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传生命周期领域服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <trantor/utils/Date.h>
#include <drogon/orm/DbClient.h>

#include "services/UploadStateMachine.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::storage {
    class IBlobStore;
    class IFileStorage;
    class UploadStagingStorage;
    struct UploadStagingSession;
}

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

    struct ChunkCoverage {
        uint64_t uploaded_count{ 0 };
        int64_t max_chunk_index{ -1 };
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

    struct LifecycleFileItem {
        uint64_t id{0};
        std::string name;
        uint64_t size{0};
        std::string hash;
        std::string mime_type;
        uint64_t parent_id{0};
        std::string created_at;
    };

    struct InitUploadCommand {
        std::string filename;
        uint64_t file_size{0};
        std::string file_hash;
        uint64_t parent_id{0};
        uint64_t user_id{0};
        uint64_t max_file_size{0};
        uint64_t chunk_size{0};
        int64_t expiry_seconds{0};
    };

    struct InitUploadOutcome {
        std::string upload_id;
        uint32_t chunk_size{0};
        uint32_t total_chunks{0};
        std::vector<uint32_t> uploaded_chunks;
        bool instant_upload{false};
        std::optional<LifecycleFileItem> file;
        UploadCacheInvalidation invalidation;
    };

    struct CompleteUploadCommand {
        std::string upload_id;
        uint64_t user_id{0};
        std::string lease_owner;
        uint32_t lease_duration_seconds{ 0 };
    };

    struct CompleteUploadOutcome {
        std::optional<LifecycleFileItem> file;
        UploadCacheInvalidation invalidation;
    };

    [[nodiscard]] auto IsExpired(const trantor::Date& expires_at, const trantor::Date& now) -> bool;

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

    [[nodiscard]] auto IsCompleteCoverage(
        uint32_t total_chunks,
        const ChunkCoverage& coverage
    ) -> bool;

    [[nodiscard]] auto DecideFinalizeStorage(std::optional<uint64_t> existing_content_id)
        -> FinalizeStorageDecision;

    class UploadLifecycleService {
    public:
        explicit UploadLifecycleService(
            drogon::orm::DbClientPtr db_client,
            disk::storage::IFileStorage* storage,
            disk::storage::UploadStagingStorage* upload_staging_storage,
            disk::storage::IBlobStore* blob_store
        );

        [[nodiscard]]
        auto InitializeUpload(InitUploadCommand command) const
            -> drogon::Task<Result<InitUploadOutcome>>;

        [[nodiscard]]
        auto CompleteUpload(CompleteUploadCommand command) const
            -> drogon::Task<Result<CompleteUploadOutcome>>;

        [[nodiscard]]
        auto CancelInProgressUpload(
            const std::string& upload_id,
            uint64_t user_id,
            uint64_t reserved_bytes,
            const disk::storage::UploadStagingSession& staging_session
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ExpireInProgressUpload(const std::string& upload_id) const
            -> drogon::Task<Result<bool>>;

        [[nodiscard]]
        auto ExpireInProgressUploads() const -> drogon::Task<Result<int>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
        disk::storage::IFileStorage* m_storage{};
        disk::storage::UploadStagingStorage* m_upload_staging_storage{};
        disk::storage::IBlobStore* m_blob_store{};
    };

} // namespace disk::upload
