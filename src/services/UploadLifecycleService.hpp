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

#include <trantor/utils/Date.h>
#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::storage {
    class IFileStorage;
}

namespace disk::upload {

    enum class UploadTaskStatus : int8_t {
        InProgress = 0,
        Completed = 1,
        Cancelled = 2,
        Expired = 3,
    };

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

    [[nodiscard]] auto ToStorageValue(UploadTaskStatus status) -> int16_t;
    [[nodiscard]] auto IsTerminalStatus(int status) -> bool;
    [[nodiscard]] auto CanComplete(int current_status) -> bool;
    [[nodiscard]] auto CanCancelOrExpire(int current_status) -> bool;
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
            disk::storage::IFileStorage* storage
        );

        [[nodiscard]]
        auto CancelInProgressUpload(
            const std::string& upload_id,
            uint64_t user_id,
            uint64_t reserved_bytes
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ExpireInProgressUpload(const std::string& upload_id) const
            -> drogon::Task<Result<bool>>;

        [[nodiscard]]
        auto ExpireInProgressUploads() const -> drogon::Task<Result<int>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
        disk::storage::IFileStorage* m_storage{};
    };

} // namespace disk::upload
