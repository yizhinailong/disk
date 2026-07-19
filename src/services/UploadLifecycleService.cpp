/**
 * @file UploadLifecycleService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传生命周期领域服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadLifecycleService.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <drogon/utils/Utilities.h>

#include "FileServiceUtils.hpp"
#include "FolderRepository.hpp"
#include "TransactionRunner.hpp"
#include "models/Files.hpp"
#include "models/UploadTasks.hpp"
#include "services/ContentService.hpp"
#include "services/QuotaService.hpp"
#include "services/UploadTaskRepository.hpp"
#include "storage/IBlobStore.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/StorageMgr.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/ConfigMgr.hpp"

namespace disk::upload {

    namespace {
        constexpr int kUploadTaskCleanupBatchSize = 100;

        [[nodiscard]] auto ExtractExtension(const std::string& filename) -> std::string {
            auto pos = filename.rfind('.');
            if (pos == std::string::npos || pos == filename.length() - 1) {
                return "";
            }
            return filename.substr(pos + 1);
        }

        [[nodiscard]] auto ToLifecycleFileItem(
            const drogon_model::disk::Files& file,
            const std::string& hash
        ) -> LifecycleFileItem {
            return LifecycleFileItem{ .id = static_cast<uint64_t>(file.getValueOfId()),
                                      .name = file.getValueOfName(),
                                      .size = static_cast<uint64_t>(file.getValueOfSize()),
                                      .hash = hash,
                                      .mime_type = file.getValueOfMimeType(),
                                      .parent_id = static_cast<uint64_t>(file.getValueOfFolderId()),
                                      .created_at = file.getValueOfCreatedAt().toDbStringLocal() };
        }

        [[nodiscard]] auto FindCompletedFile(
            const drogon::orm::DbClientPtr& client,
            uint64_t file_id,
            uint64_t user_id
        ) -> drogon::Task<std::optional<LifecycleFileItem>> {
            auto result = co_await client->execSqlCoro(
                "SELECT f.id, f.name, f.size, f.mime_type, f.folder_id, f.created_at, " "COALESCE(fc.hash_md5, '') AS hash " "FROM files AS f " "LEFT JOIN file_contents AS fc ON fc.id = f.content_id " "WHERE f.id = $1 AND f.user_id = $2",
                file_id,
                user_id
            );
            if (result.empty()) {
                co_return std::nullopt;
            }

            const auto& row = result[0];
            co_return LifecycleFileItem{ .id = row["id"].as<uint64_t>(),
                                         .name = row["name"].as<std::string>(),
                                         .size = row["size"].as<uint64_t>(),
                                         .hash = row["hash"].as<std::string>(),
                                         .mime_type = row["mime_type"].as<std::string>(),
                                         .parent_id = row["folder_id"].as<uint64_t>(),
                                         .created_at = row["created_at"].as<std::string>() };
        }

        [[nodiscard]] auto LeaseConflictError() -> ErrorInfo {
            return ErrorInfo(
                ErrorCode::ResourceConflict,
                "Upload finalization is owned by another lease; retry later"
            );
        }

        auto RenewFinalizeLease(
            const disk::file::UploadTaskRepository& repository,
            const CompleteUploadCommand& command,
            uint64_t state_version
        ) -> drogon::Task<Result<uint64_t>> {
            try {
                auto renewed_version = co_await repository.RenewFinalizeLease(
                    command.upload_id,
                    command.user_id,
                    command.lease_owner,
                    state_version,
                    command.lease_duration_seconds
                );
                if (!renewed_version.has_value()) {
                    co_return std::unexpected(LeaseConflictError());
                }
                co_return renewed_version.value();
            } catch (const std::exception& e) {
                Logger::Error() << "Failed to renew upload finalize lease: upload_id="
                                << command.upload_id << ", error=" << e.what();
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to renew upload finalize lease")
                );
            }
        }

        auto RecordFinalizeErrorBestEffort(
            const disk::file::UploadTaskRepository& repository,
            const CompleteUploadCommand& command,
            uint64_t state_version,
            ErrorCode error_code
        ) -> drogon::Task<void> {
            try {
                const auto recorded = co_await repository.RecordFinalizeErrorIfLeaseOwned(
                    command.upload_id,
                    command.user_id,
                    command.lease_owner,
                    state_version,
                    ErrorInfo(error_code).CodeInt()
                );
                if (!recorded) {
                    Logger::Warn() << "Finalize error was not recorded because lease ownership changed: upload_id="
                                   << command.upload_id << ", state_version=" << state_version;
                }
            } catch (const std::exception& e) {
                Logger::Warn() << "Failed to record upload finalize error: upload_id="
                               << command.upload_id << ", error=" << e.what();
            }
        }

        [[nodiscard]] auto IsFilenameExists(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            const std::string& filename,
            uint64_t user_id
        ) -> drogon::Task<bool> {
            try {
                auto result = co_await client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM files WHERE user_id = $1 AND folder_id = $2 AND name = $3",
                    user_id,
                    folder_id,
                    filename
                );

                if (!result.empty()) {
                    co_return result[0]["cnt"].as<uint64_t>() > 0;
                }
                co_return false;
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error() << "Failed to check filename: " << e.base().what();
                co_return false;
            }
        }

        [[nodiscard]] auto InsertFileRecord(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t content_id,
            uint64_t folder_id,
            const std::string& filename,
            const std::string& extension,
            uint64_t size,
            const std::string& mime_type,
            const std::string& path
        ) -> drogon::Task<drogon_model::disk::Files> {
            auto result = co_await client->execSqlCoro(
                "INSERT INTO files (" "user_id, content_id, folder_id, name, extension, size, mime_type, path, " "is_favorite, download_count" ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10) RETURNING *",
                user_id,
                content_id,
                folder_id,
                filename,
                extension,
                size,
                mime_type,
                path,
                static_cast<int16_t>(0),
                static_cast<uint32_t>(0)
            );

            co_return drogon_model::disk::Files(result[0], -1);
        }
    } // namespace

    UploadLifecycleService::UploadLifecycleService(
        drogon::orm::DbClientPtr db_client,
        disk::storage::IFileStorage* storage,
        disk::storage::UploadStagingStorage* upload_staging_storage,
        disk::storage::IBlobStore* blob_store
    ) : m_db_client(std::move(db_client)),
        m_storage(storage),
        m_upload_staging_storage(upload_staging_storage),
        m_blob_store(blob_store) {
        Logger::Debug() << "UploadLifecycleService initialization completed";
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

    auto UploadLifecycleService::InitializeUpload(InitUploadCommand command) const
        -> drogon::Task<Result<InitUploadOutcome>> {

        Logger::Debug() << "Starting initialize upload lifecycle: filename=\"" << command.filename
                        << "\", file_size=" << command.file_size << ", file_hash=" << command.file_hash
                        << ", parent_id=" << command.parent_id << ", user_id=" << command.user_id;

        if (command.file_size > command.max_file_size) {
            Logger::Warn() << "Upload file exceeds max size: filename=\"" << command.filename
                           << "\", file_size=" << command.file_size
                           << ", max_file_size=" << command.max_file_size << ", user_id=" << command.user_id;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "File size exceeds maximum allowed size")
            );
        }

        disk::file::UploadTaskRepository upload_task_repository(m_db_client);

        auto combined = co_await m_db_client->execSqlCoro(
            "WITH folder_loc AS (" "  SELECT path, depth FROM folders WHERE id = $1 AND user_id = $2" "), filename_exists AS (" "  SELECT COUNT(*) AS cnt FROM files" "  WHERE user_id = $2 AND folder_id = $1 AND name = $3" ")" " SELECT" "   (SELECT path FROM folder_loc) AS folder_path," "   (SELECT depth FROM folder_loc) AS folder_depth," "   (SELECT cnt FROM filename_exists) AS filename_count",
            command.parent_id,
            command.user_id,
            command.filename
        );

        const auto& row = combined[0];
        if (command.parent_id != 0 && row["folder_path"].isNull()) {
            Logger::Warn() << "Folder not found: parent_id=" << command.parent_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
        }

        auto filename_count = row["filename_count"].as<int64_t>();
        if (filename_count > 0) {
            Logger::Warn() << "File with same name already exists during upload init: "
                           << command.filename << ", parent_id=" << command.parent_id
                           << ", user_id=" << command.user_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
        }

        disk::content::ContentService content_service(m_db_client);
        auto existing_content = co_await content_service.FindByMd5(command.file_hash);
        auto existing_task_id = co_await upload_task_repository.FindInProgressIdByUserAndHash(
            command.user_id,
            command.file_hash
        );
        UploadCacheInvalidation pending_invalidation;
        auto init_decision = DecideInitFlow(
            existing_content.has_value(),
            existing_task_id.value_or(std::string{})
        );

        if (init_decision.type == InitDecisionType::InstantUpload) {
            auto content_id = existing_content->id;
            const auto& content_mime_type = existing_content->mime_type;
            Logger::Debug() << "Instant upload check successful: file_hash=" << command.file_hash
                            << ", content_id=" << content_id;

            drogon_model::disk::Files file;
            disk::file::TransactionRunner transaction_runner(
                m_db_client,
                ErrorInfo(ErrorCode::InternalError, "Failed to create file record")
            );
            auto tx_result = co_await transaction_runner.Run(
                [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                    if (co_await IsFilenameExists(
                            transaction,
                            command.parent_id,
                            command.filename,
                            command.user_id
                        )) {
                        Logger::Warn() << "File with same name already exists: " << command.filename;
                        co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
                    }

                    disk::quota::QuotaService quota_service(m_db_client);
                    auto quota_result = co_await quota_service.ConsumeUsedStorage(
                        transaction,
                        command.user_id,
                        command.file_size
                    );
                    if (!quota_result) {
                        co_return std::unexpected(quota_result.error());
                    }

                    auto increment_result = co_await content_service.IncrementRefCount(
                        transaction,
                        content_id
                    );
                    if (!increment_result) {
                        Logger::Warn() << "File content not found for instant upload: content_id="
                                       << content_id;
                        co_return std::unexpected(increment_result.error());
                    }

                    auto parent_location_result = co_await disk::file::utils::ResolveFolderLocation(
                        transaction,
                        command.parent_id,
                        command.user_id
                    );
                    if (!parent_location_result) {
                        co_return std::unexpected(parent_location_result.error());
                    }

                    file = co_await InsertFileRecord(
                        transaction,
                        command.user_id,
                        content_id,
                        command.parent_id,
                        command.filename,
                        ExtractExtension(command.filename),
                        command.file_size,
                        content_mime_type,
                        disk::file::utils::BuildFilePath(parent_location_result->path, command.filename)
                    );

                    if (command.parent_id > 0) {
                        disk::folder::FolderRepository folder_repository(m_db_client);
                        co_await folder_repository.ApplyItemCountDelta(
                            transaction,
                            command.parent_id,
                            command.user_id,
                            1,
                            trantor::Date::now()
                        );
                    }

                    co_return {};
                }
            );
            if (!tx_result) {
                Logger::Error() << "Instant upload create file record failed: "
                                << tx_result.error().message;
                co_return std::unexpected(tx_result.error());
            }

            InitUploadOutcome outcome;
            outcome.instant_upload = true;
            outcome.file = ToLifecycleFileItem(file, command.file_hash);
            outcome.invalidation.file_list_folder_ids.push_back(command.parent_id);

            Logger::Debug() << "Instant upload completed: file_id=" << file.getValueOfId();
            co_return outcome;
        }

        if (init_decision.type == InitDecisionType::ResumeUpload) {
            auto existing_task = co_await upload_task_repository.FindInProgressByUserAndHash(
                command.user_id,
                command.file_hash
            );
            if (existing_task.has_value()) {
                const auto& task = existing_task.value();
                const auto& task_id = task.getValueOfId();

                if (IsExpired(task.getValueOfExpiresAt(), trantor::Date::now())) {
                    Logger::Info() << "Expired upload task found, expiring through lifecycle: upload_id=" << task_id;
                    pending_invalidation.upload_task_ids.push_back(task_id);

                    auto expire_result = co_await ExpireInProgressUpload(task_id);
                    if (!expire_result) {
                        Logger::Error() << "Failed to expire existing upload task during init: upload_id="
                                        << task_id << ", error=" << expire_result.error().message;
                        co_return std::unexpected(expire_result.error());
                    }
                } else {
                    Logger::Debug() << "Resume upload check successful: upload_id=" << task_id;
                    auto uploaded_chunks = co_await upload_task_repository.ListUploadedChunkIndices(task_id);

                    InitUploadOutcome outcome;
                    outcome.upload_id = task_id;
                    outcome.chunk_size = task.getValueOfChunkSize();
                    outcome.total_chunks = task.getValueOfTotalChunks();
                    outcome.instant_upload = false;
                    outcome.uploaded_chunks = std::move(uploaded_chunks);
                    outcome.invalidation.upload_task_ids.push_back(task_id);

                    co_return outcome;
                }
            }
        }

        disk::quota::QuotaService quota_service(m_db_client);
        auto quota_result = co_await quota_service.ReserveUploadStorage(
            m_db_client,
            command.user_id,
            command.file_size
        );
        if (!quota_result) {
            Logger::Warn() << "Storage quota reservation failed: user_id=" << command.user_id;
            co_return std::unexpected(quota_result.error());
        }

        if (command.chunk_size == 0) {
            Logger::Error() << "Invalid upload chunk size configured: 0";
            co_await quota_service.ReleaseReservedStorage(m_db_client, command.user_id, command.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Invalid upload chunk size configuration")
            );
        }

        const auto total_chunks_u64 = ((command.file_size - 1) / command.chunk_size) + 1;
        if (total_chunks_u64 > std::numeric_limits<uint32_t>::max()) {
            Logger::Warn() << "Upload requires too many chunks: filename=\"" << command.filename
                           << "\", file_size=" << command.file_size
                           << ", chunk_size=" << command.chunk_size
                           << ", total_chunks=" << total_chunks_u64;
            co_await quota_service.ReleaseReservedStorage(m_db_client, command.user_id, command.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "File requires too many chunks")
            );
        }
        auto total_chunks = static_cast<uint32_t>(total_chunks_u64);
        auto upload_id = drogon::utils::getUuid();
        const auto config = disk::utils::ConfigMgr::GetInstance();
        const auto staging_backend = config->GetUploadStagingBackend() == disk::utils::StorageBackend::S3 ? disk::storage::UploadStagingBackend::S3 : disk::storage::UploadStagingBackend::Local;
        const auto staging_prefix = staging_backend == disk::storage::UploadStagingBackend::S3 ? config->GetS3StorageConfig().staging_prefix + "/" + upload_id : upload_id;
        const disk::storage::UploadStagingSession staging_session{
            .upload_id = upload_id,
            .backend = staging_backend,
            .prefix = staging_prefix,
        };

        drogon_model::disk::UploadTasks task;
        task.setId(upload_id);
        task.setUserId(command.user_id);
        task.setFolderId(command.parent_id);
        task.setFilename(command.filename);
        task.setFileSize(command.file_size);
        task.setFileHash(command.file_hash);
        task.setChunkSize(command.chunk_size);
        task.setTotalChunks(total_chunks);
        task.setReservedBytes(command.file_size);
        task.setTempPath(upload_id);
        task.setStatus(ToStorageValue(UploadTaskStatus::InProgress));
        task.setExpiresAt(trantor::Date::now().after(command.expiry_seconds));

        bool create_task_failed = false;
        try {
            task = co_await upload_task_repository.Create(std::move(task), staging_session);

            Logger::Debug() << "Upload task created successfully: upload_id=" << task.getValueOfId()
                            << ", total_chunks=" << total_chunks;

            if (m_upload_staging_storage != nullptr) {
                auto ensure_result = co_await m_upload_staging_storage->EnsureUploadSession(
                    staging_session
                );
                if (!ensure_result) {
                    Logger::Warn() << "Failed to ensure upload temp directory: upload_id="
                                   << task.getValueOfId();
                }
            }

            InitUploadOutcome outcome;
            outcome.upload_id = task.getValueOfId();
            outcome.chunk_size = task.getValueOfChunkSize();
            outcome.total_chunks = task.getValueOfTotalChunks();
            outcome.uploaded_chunks = {};
            outcome.instant_upload = false;
            outcome.invalidation = std::move(pending_invalidation);
            outcome.invalidation.upload_task_ids.push_back(task.getValueOfId());

            co_return outcome;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to create upload task: " << e.base().what();
            create_task_failed = true;
        }

        if (create_task_failed) {
            co_await quota_service.ReleaseReservedStorage(m_db_client, command.user_id, command.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create upload task")
            );
        }

        co_return std::unexpected(
            ErrorInfo(ErrorCode::InternalError, "Unexpected upload initialization state")
        );
    }

    auto UploadLifecycleService::CompleteUpload(CompleteUploadCommand command) const
        -> drogon::Task<Result<CompleteUploadOutcome>> {

        auto start = std::chrono::steady_clock::now();

        Logger::Debug() << "Starting complete upload lifecycle: upload_id=" << command.upload_id
                        << ", user_id=" << command.user_id;

        disk::file::UploadTaskRepository upload_task_repository(m_db_client);
        disk::file::FinalizeClaimResult claim_result;
        try {
            claim_result = co_await upload_task_repository.ClaimFinalizeLease(
                command.upload_id,
                command.user_id,
                command.lease_owner,
                command.lease_duration_seconds
            );
        } catch (const std::exception& e) {
            Logger::Error() << "Failed to claim upload finalize lease: upload_id="
                            << command.upload_id << ", error=" << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to claim upload finalize lease")
            );
        }

        switch (claim_result.disposition) {
            case disk::file::FinalizeClaimDisposition::Acquired:
                break;
            case disk::file::FinalizeClaimDisposition::IncompleteChunks:
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
                );
            case disk::file::FinalizeClaimDisposition::LeaseHeld:
                co_return std::unexpected(LeaseConflictError());
            case disk::file::FinalizeClaimDisposition::CompletedReplay: {
                if (!claim_result.completed_file_id.has_value()) {
                    Logger::Error() << "Completed upload task has no completed_file_id: upload_id="
                                    << command.upload_id;
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Completed upload result is unavailable")
                    );
                }

                try {
                    auto completed_file = co_await FindCompletedFile(
                        m_db_client,
                        claim_result.completed_file_id.value(),
                        command.user_id
                    );
                    if (!completed_file.has_value()) {
                        Logger::Error() << "Completed upload file is missing: upload_id="
                                        << command.upload_id << ", file_id="
                                        << claim_result.completed_file_id.value();
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Completed upload result is unavailable")
                        );
                    }

                    CompleteUploadOutcome outcome;
                    outcome.file = std::move(completed_file.value());
                    outcome.invalidation.upload_task_ids.push_back(command.upload_id);
                    co_return outcome;
                } catch (const std::exception& e) {
                    Logger::Error() << "Failed to load completed upload result: upload_id="
                                    << command.upload_id << ", error=" << e.what();
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to load completed upload result")
                    );
                }
            }
            case disk::file::FinalizeClaimDisposition::Terminal:
            case disk::file::FinalizeClaimDisposition::NotFound:
                co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
        }

        auto task = co_await upload_task_repository.FindByIdForUser(command.upload_id, command.user_id);
        if (!task.has_value()) {
            Logger::Error() << "Claimed upload task could not be loaded: upload_id="
                            << command.upload_id;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to load claimed upload task")
            );
        }
        auto upload_task = std::move(task.value());
        auto state_version = claim_result.state_version;

        std::optional<disk::storage::UploadStagingSession> staging_session;
        try {
            staging_session = co_await upload_task_repository.FindStagingSessionForUser(
                command.upload_id,
                command.user_id
            );
        } catch (const std::exception& e) {
            Logger::Error() << "Failed to load staging session: upload_id=" << command.upload_id
                            << ", error=" << e.what();
        }
        if (!staging_session.has_value()) {
            co_await RecordFinalizeErrorBestEffort(
                upload_task_repository,
                command,
                state_version,
                ErrorCode::InternalError
            );
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to load upload staging session")
            );
        }

        std::vector<disk::storage::UploadStagingChunk> chunks;
        bool chunk_descriptor_load_failed = false;
        try {
            chunks = co_await upload_task_repository.ListChunksForAssembly(command.upload_id);
        } catch (const std::exception& e) {
            Logger::Error() << "Failed to load chunk descriptors for assembly: upload_id="
                            << command.upload_id << ", error=" << e.what();
            chunk_descriptor_load_failed = true;
        }
        if (chunk_descriptor_load_failed) {
            co_await RecordFinalizeErrorBestEffort(
                upload_task_repository,
                command,
                state_version,
                ErrorCode::InternalError
            );
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to load upload chunk descriptors")
            );
        }

        if (m_upload_staging_storage == nullptr) {
            Logger::Error() << "Upload staging storage is not configured";
            co_await RecordFinalizeErrorBestEffort(
                upload_task_repository,
                command,
                state_version,
                ErrorCode::InternalError
            );
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Upload staging storage is not configured")
            );
        }

        auto assemble_start = std::chrono::steady_clock::now();
        auto assemble_result = co_await m_upload_staging_storage->AssembleChunks(
            staging_session.value(),
            state_version,
            upload_task.getValueOfTotalChunks(),
            chunks
        );
        Logger::Debug() << "[stage_timer] assemble duration_ms="
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - assemble_start
                           )
                               .count()
                        << " upload_id=" << command.upload_id
                        << " total_chunks=" << upload_task.getValueOfTotalChunks();
        if (!assemble_result) {
            Logger::Error() << "Failed to assemble chunks: upload_id=" << command.upload_id
                            << ", error=" << static_cast<int>(assemble_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                           << " outcome=failure upload_id=" << command.upload_id
                           << " total_chunks=" << upload_task.getValueOfTotalChunks();

            co_await RecordFinalizeErrorBestEffort(
                upload_task_repository,
                command,
                state_version,
                assemble_result.error().code
            );
            co_return std::unexpected(assemble_result.error());
        }
        const auto& assembled = assemble_result.value();
        const auto& final_hash = assembled.md5_hash;
        const auto& precomputed_sha256 = assembled.sha256_hash;

        auto renew_after_assembly = co_await RenewFinalizeLease(
            upload_task_repository,
            command,
            state_version
        );
        if (!renew_after_assembly) {
            auto cleanup_result = co_await m_upload_staging_storage->DiscardAssembly(
                staging_session.value(),
                assembled
            );
            if (!cleanup_result) {
                Logger::Warn() << "Failed to discard assembly after lease loss: upload_id="
                               << command.upload_id;
            }
            co_return std::unexpected(renew_after_assembly.error());
        }
        state_version = renew_after_assembly.value();

        if (assembled.size_bytes != static_cast<uint64_t>(upload_task.getValueOfFileSize()) ||
            final_hash != upload_task.getValueOfFileHash()) {
            Logger::Error() << "File integrity mismatch: expected_size="
                            << upload_task.getValueOfFileSize()
                            << ", actual_size=" << assembled.size_bytes
                            << ", expected_md5=" << upload_task.getValueOfFileHash()
                            << ", actual_md5=" << final_hash;
            auto delete_result = co_await m_upload_staging_storage->DiscardAssembly(
                staging_session.value(),
                assembled
            );
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file after hash mismatch: "
                               << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                           << " outcome=failure upload_id=" << command.upload_id
                           << " total_chunks=" << upload_task.getValueOfTotalChunks();

            co_await RecordFinalizeErrorBestEffort(
                upload_task_repository,
                command,
                state_version,
                ErrorCode::ChunkVerifyFailed
            );
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "File hash verification failed")
            );
        }

        Logger::Debug() << "File hash verification passed: " << final_hash;

        struct FinalizeLookupResult {
            std::optional<uint64_t> existing_content_id;
            bool filename_exists = false;
        };

        auto dedup_start = std::chrono::steady_clock::now();
        auto lookup_result = co_await [this, &final_hash, &upload_task, &command]() -> drogon::Task<FinalizeLookupResult> {
            FinalizeLookupResult lookup;
            disk::content::ContentService content_service(m_db_client);
            auto existing_content = co_await content_service.FindByMd5(final_hash);
            if (existing_content.has_value()) {
                lookup.existing_content_id = existing_content->id;
            }

            try {
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT EXISTS(SELECT 1 FROM files WHERE user_id = $1 AND folder_id = $2 AND name = $3) AS filename_exists",
                    command.user_id,
                    upload_task.getValueOfFolderId(),
                    upload_task.getValueOfFilename()
                );

                if (!result.empty()) {
                    lookup.filename_exists = result[0]["filename_exists"].as<bool>();
                }

                co_return lookup;
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error() << "Failed to query finalize upload metadata: " << e.base().what();
                co_return lookup;
            }
        }();

        if (lookup_result.filename_exists) {
            Logger::Warn() << "File with same name already exists: " << upload_task.getValueOfFilename();
            auto delete_result = co_await m_upload_staging_storage->DiscardAssembly(
                staging_session.value(),
                assembled
            );
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file on duplicate name: "
                               << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                           << " outcome=failure upload_id=" << command.upload_id
                           << " total_chunks=" << upload_task.getValueOfTotalChunks();

            Logger::Info() << "[stage_timer] dedup_lookup duration_ms="
                           << std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - dedup_start
                              )
                                  .count()
                           << " upload_id=" << command.upload_id
                           << " dedup_hit=" << (lookup_result.existing_content_id.has_value() ? "true" : "false")
                           << " filename_exists=true";

            co_await RecordFinalizeErrorBestEffort(
                upload_task_repository,
                command,
                state_version,
                ErrorCode::FileAlreadyExists
            );
            co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
        }

        auto existing_content = lookup_result.existing_content_id;
        auto finalize_storage_decision = DecideFinalizeStorage(existing_content);
        std::filesystem::path final_storage_path;
        std::string final_sha256;

        if (finalize_storage_decision.type == FinalizeStorageDecisionType::ReuseExistingContent) {
            auto delete_result = co_await m_upload_staging_storage->DiscardAssembly(
                staging_session.value(),
                assembled
            );
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file after dedup: "
                               << static_cast<int>(delete_result.error().code);
            }
            Logger::Debug() << "File dedup successful: content_id="
                            << finalize_storage_decision.existing_content_id.value();
        } else {
            if (m_blob_store == nullptr) {
                Logger::Error() << "Blob store is not configured";
                auto cleanup_result = co_await m_upload_staging_storage->DiscardAssembly(
                    staging_session.value(),
                    assembled
                );
                if (!cleanup_result) {
                    Logger::Warn() << "Failed to cleanup assemble file after missing blob store: "
                                   << static_cast<int>(cleanup_result.error().code);
                }
                co_await RecordFinalizeErrorBestEffort(
                    upload_task_repository,
                    command,
                    state_version,
                    ErrorCode::InternalError
                );
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Blob store is not configured")
                );
            }

            auto promote_result = co_await m_blob_store->PromoteToFinal(assembled, final_hash);
            if (!promote_result) {
                Logger::Error() << "Failed to move file to final storage: error="
                                << static_cast<int>(promote_result.error().code);
                auto cleanup_result = co_await m_upload_staging_storage->DiscardAssembly(
                    staging_session.value(),
                    assembled
                );
                if (!cleanup_result) {
                    Logger::Warn() << "Failed to cleanup assemble file after promote failure: "
                                   << static_cast<int>(cleanup_result.error().code);
                }

                auto end = std::chrono::steady_clock::now();
                auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                Logger::Info() << "[complete_upload] duration_us=" << duration_us
                               << " outcome=failure upload_id=" << command.upload_id
                               << " total_chunks=" << upload_task.getValueOfTotalChunks();

                co_await RecordFinalizeErrorBestEffort(
                    upload_task_repository,
                    command,
                    state_version,
                    promote_result.error().code
                );
                co_return std::unexpected(promote_result.error());
            }

            const auto& promoted = promote_result.value();
            final_storage_path = promoted.path;
            final_sha256 = precomputed_sha256;
        }
        Logger::Debug() << "[stage_timer] dedup_lookup duration_ms="
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - dedup_start
                           )
                               .count()
                        << " upload_id=" << command.upload_id
                        << " dedup_hit=" << (existing_content.has_value() ? "true" : "false")
                        << " filename_exists=false";

        auto renew_before_transaction = co_await RenewFinalizeLease(
            upload_task_repository,
            command,
            state_version
        );
        if (!renew_before_transaction) {
            co_return std::unexpected(renew_before_transaction.error());
        }
        state_version = renew_before_transaction.value();

        drogon_model::disk::Files file;
        bool db_operation_failed = false;
        auto tx_start = std::chrono::steady_clock::now();
        disk::file::TransactionRunner transaction_runner(m_db_client);
        auto tx_result = co_await transaction_runner.Run(
            [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                disk::content::ContentService content_service(m_db_client);

                uint64_t content_id = 0;
                if (existing_content.has_value()) {
                    content_id = existing_content.value();
                    auto increment_result = co_await content_service.IncrementRefCount(transaction, content_id);
                    if (!increment_result) {
                        Logger::Warn() << "File content not found when finalizing upload: content_id="
                                       << content_id;
                        co_return std::unexpected(increment_result.error());
                    }
                } else {
                    auto content = co_await content_service.Create(
                        transaction,
                        disk::content::NewContent{ .hash_md5 = final_hash,
                                                   .hash_sha256 = final_sha256,
                                                   .size = static_cast<uint64_t>(upload_task.getValueOfFileSize()),
                                                   .storage_path = final_storage_path.string(),
                                                   .mime_type = "" }
                    );
                    content_id = content.id;
                    Logger::Debug() << "FileContents created successfully: content_id=" << content_id;
                }

                auto parent_location_result = co_await disk::file::utils::ResolveFolderLocation(
                    transaction,
                    upload_task.getValueOfFolderId(),
                    command.user_id
                );
                if (!parent_location_result) {
                    co_return std::unexpected(parent_location_result.error());
                }

                file = co_await InsertFileRecord(
                    transaction,
                    command.user_id,
                    content_id,
                    upload_task.getValueOfFolderId(),
                    upload_task.getValueOfFilename(),
                    ExtractExtension(upload_task.getValueOfFilename()),
                    upload_task.getValueOfFileSize(),
                    "",
                    disk::file::utils::BuildFilePath(parent_location_result->path, upload_task.getValueOfFilename())
                );

                if (upload_task.getValueOfFolderId() > 0) {
                    disk::folder::FolderRepository folder_repository(m_db_client);
                    co_await folder_repository.ApplyItemCountDelta(
                        transaction,
                        upload_task.getValueOfFolderId(),
                        command.user_id,
                        1,
                        trantor::Date::now()
                    );
                }

                disk::quota::QuotaService quota_service(m_db_client);
                auto transfer_result = co_await quota_service.CommitReservedToUsed(
                    transaction,
                    command.user_id,
                    upload_task.getValueOfFileSize()
                );
                if (!transfer_result) {
                    co_return std::unexpected(transfer_result.error());
                }

                auto finalize_success = co_await upload_task_repository.MarkCompletedIfLeaseOwned(
                    transaction,
                    command.upload_id,
                    command.user_id,
                    command.lease_owner,
                    state_version,
                    file.getValueOfId()
                );
                if (!finalize_success) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceConflict,
                        "Upload finalization lease changed before commit"
                    ));
                }

                co_await upload_task_repository.DeleteChunks(transaction, command.upload_id);

                co_return {};
            }
        );
        if (!tx_result) {
            Logger::Error() << "Upload finalization transaction failed: " << tx_result.error().message;
            db_operation_failed = true;
        }
        Logger::Debug() << "[stage_timer] tx duration_ms="
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - tx_start
                           )
                               .count()
                        << " upload_id=" << command.upload_id
                        << " success=" << (!db_operation_failed ? "true" : "false");

        if (db_operation_failed) {
            auto compensation_start = std::chrono::steady_clock::now();
            if (!existing_content.has_value() && !final_storage_path.empty()) {
                Logger::Warn() << "Keeping promoted blob after transaction failure for retry/reconciliation: upload_id="
                               << command.upload_id << ", storage_path=" << final_storage_path;
            }
            Logger::Info() << "[stage_timer] compensation_cleanup duration_ms="
                           << std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - compensation_start
                              )
                                  .count()
                           << " upload_id=" << command.upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                           << " outcome=failure upload_id=" << command.upload_id
                           << " total_chunks=" << upload_task.getValueOfTotalChunks();

            co_await RecordFinalizeErrorBestEffort(
                upload_task_repository,
                command,
                state_version,
                tx_result.error().code
            );
            co_return std::unexpected(tx_result.error());
        }

        Logger::Debug() << "Files record created successfully: file_id=" << file.getValueOfId();

        auto temp_cleanup_start = std::chrono::steady_clock::now();
        auto cleanup_result = co_await m_upload_staging_storage->CleanupSession(
            staging_session.value()
        );
        Logger::Debug() << "[stage_timer] temp_cleanup duration_ms="
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - temp_cleanup_start
                           )
                               .count()
                        << " upload_id=" << command.upload_id;
        if (!cleanup_result) {
            Logger::Warn() << "Failed to cleanup temp artifacts: "
                           << static_cast<int>(cleanup_result.error().code);
        }

        CompleteUploadOutcome outcome;
        outcome.file = ToLifecycleFileItem(file, final_hash);
        outcome.invalidation.upload_task_ids.push_back(command.upload_id);
        outcome.invalidation.file_list_folder_ids.push_back(file.getValueOfFolderId());

        Logger::Debug() << "File upload completed: file_id=" << file.getValueOfId()
                        << ", filename=" << upload_task.getValueOfFilename();

        auto end = std::chrono::steady_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        Logger::Debug() << "[complete_upload] duration_us=" << duration_us
                        << " outcome=success upload_id=" << command.upload_id
                        << " total_chunks=" << upload_task.getValueOfTotalChunks();

        co_return outcome;
    }

    auto UploadLifecycleService::CancelInProgressUpload(
        const std::string& upload_id,
        uint64_t user_id,
        uint64_t reserved_bytes,
        const disk::storage::UploadStagingSession& staging_session
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

        if (m_upload_staging_storage != nullptr) {
            auto cleanup_result = co_await m_upload_staging_storage->CleanupSession(staging_session);
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
        std::optional<disk::storage::UploadStagingSession> staging_session;

        disk::file::UploadTaskRepository upload_task_repository(m_db_client);
        disk::file::TransactionRunner transaction_runner(m_db_client);
        auto tx_result = co_await transaction_runner.Run(
            [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                auto expired_record = co_await upload_task_repository.MarkExpiredIfInProgressReturning(
                    transaction,
                    upload_id,
                    "任务过期"
                );

                if (!expired_record.has_value()) {
                    co_return {};
                }

                user_id = expired_record->user_id;
                reserved_bytes = expired_record->reserved_bytes;
                temp_path = expired_record->temp_path;
                staging_session = expired_record->staging_session;

                disk::quota::QuotaService quota_service(m_db_client);
                auto release_result = co_await quota_service.ReleaseReservedStorageChecked(
                    transaction,
                    user_id,
                    reserved_bytes
                );
                if (!release_result) {
                    co_return std::unexpected(release_result.error());
                }

                co_await upload_task_repository.DeleteChunks(transaction, upload_id);

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

        auto* storage = m_upload_staging_storage != nullptr ? m_upload_staging_storage : disk::storage::StorageMgr::GetUploadStagingStorage();
        if (storage != nullptr && staging_session.has_value()) {
            auto cleanup_result = co_await storage->CleanupSession(staging_session.value());
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
        disk::file::UploadTaskRepository upload_task_repository(m_db_client);
        auto expired_tasks =
            co_await upload_task_repository.FindExpiredInProgressBatch(kUploadTaskCleanupBatchSize);

        int cleaned_count = 0;
        for (const auto& task : expired_tasks) {
            auto expire_result = co_await ExpireInProgressUpload(task.id);
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
