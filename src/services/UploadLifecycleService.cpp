/**
 * @file UploadLifecycleService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传生命周期领域服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadLifecycleService.hpp"

#include <unordered_map>
#include <utility>
#include <vector>

#include "FileServiceUtils.hpp"
#include "services/QuotaService.hpp"
#include "services/UploadTaskRepository.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::upload {

    namespace {
        constexpr int kUploadTaskCleanupBatchSize = 100;
    }

    UploadLifecycleService::UploadLifecycleService(
        drogon::orm::DbClientPtr db_client,
        disk::storage::IFileStorage* storage
    ) : m_db_client(std::move(db_client)), m_storage(storage) {
        Logger::Debug() << "UploadLifecycleService initialization completed";
    }

    auto ToStorageValue(UploadTaskStatus status) -> int8_t {
        return static_cast<int8_t>(status);
    }

    auto IsTerminalStatus(int status) -> bool {
        return status == ToStorageValue(UploadTaskStatus::Completed) ||
               status == ToStorageValue(UploadTaskStatus::Cancelled) ||
               status == ToStorageValue(UploadTaskStatus::Expired);
    }

    auto CanComplete(int current_status) -> bool {
        return current_status == ToStorageValue(UploadTaskStatus::InProgress);
    }

    auto CanCancelOrExpire(int current_status) -> bool {
        return current_status == ToStorageValue(UploadTaskStatus::InProgress);
    }

    auto IsExpired(const trantor::Date& expires_at, const trantor::Date& now) -> bool {
        return expires_at < now;
    }

    auto DecideInitFlow(
        bool has_existing_content,
        std::string_view existing_task_id
    ) -> InitDecision {
        if (has_existing_content) {
            return InitDecision{ .type = InitDecisionType::InstantUpload };
        }

        if (!existing_task_id.empty()) {
            return InitDecision{ .type = InitDecisionType::ResumeUpload,
                                 .upload_id = std::string(existing_task_id) };
        }

        return InitDecision{ .type = InitDecisionType::StartNewUpload };
    }

    auto ValidateChunkAcceptance(
        uint32_t chunk_index,
        uint64_t data_size,
        uint64_t file_size,
        uint32_t chunk_size,
        uint32_t total_chunks
    ) -> std::expected<ChunkAcceptance, ChunkAcceptanceError> {
        if (chunk_index >= total_chunks) {
            return std::unexpected(ChunkAcceptanceError{
                .error = ErrorInfo(ErrorCode::ValidationFailed, "Chunk index out of range"),
                .expected_size = 0,
            });
        }

        const auto chunk_offset = static_cast<uint64_t>(chunk_index) * chunk_size;
        const auto remaining_bytes = file_size - chunk_offset;
        const auto expected_size = std::min<uint64_t>(chunk_size, remaining_bytes);
        if (data_size != expected_size) {
            return std::unexpected(ChunkAcceptanceError{
                .error = ErrorInfo(ErrorCode::ValidationFailed, "Unexpected chunk size"),
                .expected_size = expected_size,
            });
        }

        return ChunkAcceptance{ .expected_size = expected_size };
    }

    auto IsCompleteCoverage(
        uint32_t total_chunks,
        const ChunkCoverage& coverage
    ) -> bool {
        if (total_chunks == 0) {
            return coverage.uploaded_count == 0;
        }

        return coverage.uploaded_count == static_cast<uint64_t>(total_chunks) &&
               coverage.max_chunk_index == static_cast<int64_t>(total_chunks - 1);
    }

    auto DecideFinalizeStorage(std::optional<uint64_t> existing_content_id)
        -> FinalizeStorageDecision {
        if (existing_content_id.has_value()) {
            return FinalizeStorageDecision{
                .type = FinalizeStorageDecisionType::ReuseExistingContent,
                .existing_content_id = existing_content_id,
            };
        }

        return FinalizeStorageDecision{ .type = FinalizeStorageDecisionType::PromoteAsNewContent };
    }

    auto UploadLifecycleService::CancelInProgressUpload(
        const std::string& upload_id,
        uint64_t user_id,
        uint64_t reserved_bytes
    ) const -> drogon::Task<Result<void>> {
        disk::quota::QuotaService quota_service(m_db_client);
        co_await quota_service.ReleaseReservedStorage(m_db_client, user_id, reserved_bytes);

        try {
            disk::file::UploadTaskRepository upload_task_repository(m_db_client);
            co_await upload_task_repository.MarkCancelledIfInProgress(upload_id, "用户取消");
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to set cancel terminal state: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to cancel upload task")
            );
        }

        try {
            disk::file::UploadTaskRepository upload_task_repository(m_db_client);
            co_await upload_task_repository.DeleteChunks(upload_id);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to cleanup upload_task_chunks: " << e.base().what();
        }

        if (m_storage != nullptr) {
            auto cleanup_result = co_await m_storage->CleanupTemp(upload_id);
            if (!cleanup_result) {
                Logger::Warn() << "Failed to delete temp directory: upload_id=" << upload_id
                         << ", error=" << static_cast<int>(cleanup_result.error().code);
            }
        }

        co_return {};
    }

    auto UploadLifecycleService::ExpireInProgressUploads() const -> drogon::Task<Result<int>> {
        disk::file::UploadTaskRepository upload_task_repository(m_db_client);
        auto expired_tasks =
            co_await upload_task_repository.FindExpiredInProgressBatch(kUploadTaskCleanupBatchSize);

        int cleaned_count = 0;
        auto* storage = m_storage != nullptr ? m_storage : disk::storage::StorageMgr::GetStorage();
        std::unordered_map<uint64_t, uint64_t> user_reserved_delta;
        std::vector<std::string> expired_task_ids;
        expired_task_ids.reserve(expired_tasks.size());

        for (const auto& task : expired_tasks) {
            expired_task_ids.push_back(task.id);

            if (storage != nullptr) {
                auto delete_result = co_await storage->CleanupTemp(task.id);
                if (!delete_result.has_value()) {
                    Logger::Warn() << "Failed to cleanup temp file for expired upload task: task_id="
                             << task.id << ", temp_path=" << task.temp_path
                             << ", error_code=" << static_cast<uint32_t>(delete_result.error().code)
                             << ", error_message=" << delete_result.error().message;
                } else {
                    Logger::Debug() << "Temp file cleaned for expired upload task: task_id="
                              << task.id << ", temp_path=" << task.temp_path;
                }
            }

            if (task.reserved_bytes > 0) {
                user_reserved_delta[task.user_id] += task.reserved_bytes;
            }

            cleaned_count++;

            Logger::Debug() << "Expired upload task marked as expired: task_id=" << task.id
                      << ", user_id=" << task.user_id << ", reserved_bytes=" << task.reserved_bytes;
        }

        if (!expired_task_ids.empty()) {
            co_await upload_task_repository.MarkExpiredIfInProgressBatch(expired_task_ids, "任务过期");
        }

        disk::quota::QuotaService quota_service(m_db_client);
        for (const auto& [user_id, delta] : user_reserved_delta) {
            co_await quota_service.ReleaseReservedStorage(m_db_client, user_id, delta);
            Logger::Debug() << "Released reserved storage for expired upload tasks: user_id=" << user_id
                      << ", released_bytes=" << delta;
        }

        co_return cleaned_count;
    }

} // namespace disk::upload
