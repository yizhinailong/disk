/**
 * @file UploadLifecycleService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传生命周期领域服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadLifecycleService.hpp"

#include <utility>
#include <vector>

#include "TransactionRunner.hpp"
#include "services/QuotaService.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/StorageMgr.hpp"

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

    auto ToStorageValue(UploadTaskStatus status) -> int16_t {
        return static_cast<int16_t>(status);
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
            co_await m_db_client->execSqlCoro(
                "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), "
                "fail_reason = '用户取消' WHERE id = $2 AND status = $3",
                ToStorageValue(UploadTaskStatus::Cancelled),
                upload_id,
                ToStorageValue(UploadTaskStatus::InProgress)
            );
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to set cancel terminal state: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to cancel upload task")
            );
        }

        try {
            co_await m_db_client->execSqlCoro(
                "DELETE FROM upload_task_chunks WHERE task_id = $1",
                upload_id
            );
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

    auto UploadLifecycleService::ExpireInProgressUpload(const std::string& upload_id) const
        -> drogon::Task<Result<bool>> {
        bool expired = false;
        std::string temp_path;
        uint64_t user_id = 0;
        uint64_t reserved_bytes = 0;

        disk::file::TransactionRunner transaction_runner(m_db_client);
        auto tx_result = co_await transaction_runner.Run(
            [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                auto result = co_await transaction->execSqlCoro(
                    "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = '任务过期' "
                    "WHERE id = $2 AND status = $3 AND expires_at < NOW() "
                    "RETURNING user_id, reserved_bytes, temp_path",
                    ToStorageValue(UploadTaskStatus::Expired),
                    upload_id,
                    ToStorageValue(UploadTaskStatus::InProgress)
                );

                if (result.empty()) {
                    co_return {};
                }

                const auto& row = result[0];
                user_id = row["user_id"].as<uint64_t>();
                reserved_bytes = row["reserved_bytes"].as<uint64_t>();
                temp_path = row["temp_path"].as<std::string>();

                disk::quota::QuotaService quota_service(m_db_client);
                auto release_result = co_await quota_service.ReleaseReservedStorageChecked(
                    transaction,
                    user_id,
                    reserved_bytes
                );
                if (!release_result) {
                    co_return std::unexpected(release_result.error());
                }

                expired = true;
                co_return {};
            }
        );

        if (!tx_result) {
            co_return std::unexpected(tx_result.error());
        }

        if (!expired) {
            co_return false;
        }

        auto* storage = m_storage != nullptr ? m_storage : disk::storage::StorageMgr::GetStorage();
        if (storage != nullptr) {
            auto cleanup_result = co_await storage->CleanupTemp(upload_id);
            if (!cleanup_result.has_value()) {
                Logger::Warn() << "Failed to cleanup temp file for expired upload task: task_id="
                         << upload_id << ", temp_path=" << temp_path
                         << ", error_code=" << static_cast<uint32_t>(cleanup_result.error().code)
                         << ", error_message=" << cleanup_result.error().message;
            } else {
                Logger::Debug() << "Temp file cleaned for expired upload task: task_id="
                          << upload_id << ", temp_path=" << temp_path;
            }
        }

        Logger::Debug() << "Expired upload task marked as expired: task_id=" << upload_id
                  << ", user_id=" << user_id << ", reserved_bytes=" << reserved_bytes;

        co_return true;
    }

    auto UploadLifecycleService::ExpireInProgressUploads() const -> drogon::Task<Result<int>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT id FROM upload_tasks "
            "WHERE status = $1 AND expires_at < NOW() "
            "LIMIT $2",
            ToStorageValue(UploadTaskStatus::InProgress),
            kUploadTaskCleanupBatchSize
        );

        int cleaned_count = 0;
        for (const auto& row : result) {
            auto task_id = row["id"].as<std::string>();
            auto expire_result = co_await ExpireInProgressUpload(task_id);
            if (!expire_result) {
                co_return std::unexpected(expire_result.error());
            }
            if (expire_result.value()) {
                cleaned_count++;
            }
        }

        co_return cleaned_count;
    }

} // namespace disk::upload
