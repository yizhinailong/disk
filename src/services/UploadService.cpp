/**
 * @file UploadService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadService.hpp"

#include <chrono>
#include <string_view>

#include <drogon/drogon.h>

#include "models/UploadTasks.hpp"
#include "services/FileListCache.hpp"
#include "services/MetricsService.hpp"
#include "services/UploadLifecycleService.hpp"
#include "services/UploadTaskRepository.hpp"
#include "storage/IBlobStore.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"

namespace disk::file {

    using disk::utils::ConfigMgr;
    using disk::utils::FileHashUtil;
    using drogon_model::disk::UploadTasks;

    namespace {

        [[nodiscard]] auto UploadRuntimeLogContext() -> disk::utils::LogContext {
            return { .operation = "upload_runtime" };
        }

    } // namespace

    /// ==================== 构造函数 ====================

    UploadService::UploadService(
        drogon::orm::DbClientPtr db_client,
        storage::IFileStorage* storage,
        storage::UploadStagingStorage* upload_staging_storage,
        storage::IBlobStore* blob_store
    ) : m_db_client(std::move(db_client)),
        m_storage(storage),
        m_upload_staging_storage(upload_staging_storage),
        m_blob_store(blob_store) {
        StartUploadTaskCacheMaintenance();
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=upload";
    }

    /// ==================== InitUpload ====================

    auto UploadService::InitUpload(
        InitUploadRequest request,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<InitUploadResponse>> {
        log_context.operation = "upload_init";

        Logger::Debug(log_context)
            << "Starting initialize upload: filename=\"" << request.filename
            << "\", file_size=" << request.file_size << ", file_hash=" << request.file_hash
            << ", parent_id=" << request.parent_id << ", user_id=" << user_id;

        auto config = ConfigMgr::GetInstance();
        disk::upload::UploadLifecycleService lifecycle_service(
            m_db_client,
            m_storage,
            m_upload_staging_storage,
            m_blob_store
        );
        auto lifecycle_result = co_await lifecycle_service.InitializeUpload(
            disk::upload::InitUploadCommand{ .filename = std::move(request.filename),
                                             .file_size = request.file_size,
                                             .file_hash = std::move(request.file_hash),
                                             .parent_id = request.parent_id,
                                             .user_id = user_id,
                                             .max_file_size = config->GetMaxFileSize(),
                                             .chunk_size = config->GetChunkSize(),
                                             .expiry_seconds = config->GetUploadTaskExpirySeconds(),
                                             .upload_task_creation_enabled =
                                                 config->GetUploadTaskCreationEnabled() },
            log_context
        );
        if (!lifecycle_result) {
            co_return std::unexpected(lifecycle_result.error());
        }

        const auto& invalidation = lifecycle_result->invalidation;
        for (const auto& upload_task_id : invalidation.upload_task_ids) {
            InvalidateUploadTaskCache(upload_task_id);
        }
        if (!invalidation.file_list_folder_ids.empty()) {
            co_await FileListCache::Invalidate(m_redis_service, user_id, log_context);
        }

        InitUploadResponse response;
        response.upload_id = lifecycle_result->upload_id;
        response.chunk_size = lifecycle_result->chunk_size;
        response.total_chunks = lifecycle_result->total_chunks;
        response.uploaded_chunks = lifecycle_result->uploaded_chunks;
        response.instant_upload = lifecycle_result->instant_upload;
        if (lifecycle_result->file.has_value()) {
            const auto& file = lifecycle_result->file.value();
            response.file = FileItem{ .id = file.id,
                                      .name = file.name,
                                      .size = file.size,
                                      .hash = file.hash,
                                      .mime_type = file.mime_type,
                                      .parent_id = file.parent_id,
                                      .created_at = file.created_at };
        }

        co_return response;
    }

    /// ==================== UploadChunk ====================

    auto UploadService::UploadChunk(
        std::string upload_id,
        uint32_t chunk_index,
        std::string chunk_hash,
        std::string_view chunk_data,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<UploadChunkResponse>> {
        log_context.operation = "upload_chunk";
        if (upload_id.empty()) {
            log_context.upload_id.reset();
        } else {
            log_context.upload_id = upload_id;
        }

        auto start = std::chrono::steady_clock::now();

        Logger::HighVolumeDetail(log_context)
            << "Starting upload chunk: upload_id=" << upload_id
            << ", chunk_index=" << chunk_index
            << ", chunk_hash=" << chunk_hash
            << ", data_size=" << chunk_data.size();

        /// 1. 优先读取短 TTL 上传任务缓存，命中后避免重复查询数据库
        auto cached_task = TryGetUploadTaskCacheEntry(upload_id, user_id);
        if (!cached_task.has_value()) {
            auto task_result = co_await FindUploadTask(upload_id, user_id, log_context);
            if (!task_result) {
                Logger::Warn(log_context) << "Upload task verification failed: " << upload_id;

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                Logger::HighVolumeFailure(log_context)
                    << "[upload_chunk] duration_us=" << duration_us
                    << " outcome=failure upload_id=" << upload_id
                    << " chunk_index=" << chunk_index
                    << " data_size=" << chunk_data.size();

                co_return std::unexpected(task_result.error());
            }

            if (task_result->getValueOfStatus() !=
                disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)) {
                Logger::Warn(log_context)
                    << "Upload task no longer accepts chunks: upload_id=" << upload_id
                    << ", status=" << task_result->getValueOfStatus();
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ResourceConflict, "Upload task no longer accepts chunks")
                );
            }

            auto staging_session_result = co_await FindUploadStagingSession(
                upload_id,
                user_id,
                log_context
            );
            if (!staging_session_result) {
                co_return std::unexpected(staging_session_result.error());
            }

            auto cache_entry = BuildUploadTaskCacheEntry(
                task_result.value(),
                std::move(staging_session_result.value())
            );
            CacheUploadTaskEntry(upload_id, cache_entry);
            cached_task = std::move(cache_entry);
        }

        const auto& task = cached_task.value();

        /// 2. 验证分片索引和大小符合任务几何信息
        auto chunk_acceptance = disk::upload::ValidateChunkAcceptance(
            chunk_index,
            chunk_data.size(),
            task.file_size,
            task.chunk_size,
            task.total_chunks
        );
        if (!chunk_acceptance) {
            const auto& acceptance_error = chunk_acceptance.error();
            if (acceptance_error.error.message == "Chunk index out of range") {
                Logger::Warn(log_context)
                    << "Chunk index out of range: chunk_index=" << chunk_index
                    << ", total_chunks=" << task.total_chunks;
            } else {
                Logger::Warn(log_context)
                    << "Unexpected chunk size: upload_id=" << upload_id
                    << ", chunk_index=" << chunk_index
                    << ", expected_size=" << acceptance_error.expected_size
                    << ", actual_size=" << chunk_data.size()
                    << ", file_size=" << task.file_size
                    << ", chunk_size=" << task.chunk_size;
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::HighVolumeFailure(log_context)
                << "[upload_chunk] duration_us=" << duration_us
                << " outcome=failure upload_id=" << upload_id
                << " chunk_index=" << chunk_index
                << " data_size=" << chunk_data.size();

            co_return std::unexpected(acceptance_error.error);
        }

        /// 5. 将请求体复制到拥有所有权的缓冲区，只做一次哈希+落盘复用。
        std::string chunk_payload{ chunk_data };
        auto actual_hash = FileHashUtil::HashMd5(chunk_payload);
        if (actual_hash != chunk_hash) {
            Logger::Warn(log_context) << "Chunk hash mismatch: expected=" << chunk_hash
                                      << ", actual=" << actual_hash;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::HighVolumeFailure(log_context)
                << "[upload_chunk] duration_us=" << duration_us
                << " outcome=failure upload_id=" << upload_id
                << " chunk_index=" << chunk_index
                << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Chunk hash mismatch")
            );
        }

        /// 6. 创建临时目录并写入分片
        if (m_upload_staging_storage == nullptr) {
            Logger::Error(log_context) << "Upload staging storage is not configured";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Upload staging storage is not configured")
            );
        }

        auto write_result = co_await m_upload_staging_storage->WriteChunk(
            task.staging_session,
            chunk_index,
            actual_hash,
            std::move(chunk_payload),
            log_context
        );
        if (!write_result) {
            Logger::Error(log_context) << "Failed to write chunk file: upload_id=" << upload_id
                                       << ", chunk_index=" << chunk_index << ", error="
                                       << static_cast<int>(write_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::HighVolumeFailure(log_context)
                << "[upload_chunk] duration_us=" << duration_us
                << " outcome=failure upload_id=" << upload_id
                << " chunk_index=" << chunk_index
                << " data_size=" << chunk_data.size();

            co_return std::unexpected(write_result.error());
        }

        /// 7. 仅在数据库任务仍为 InProgress 时记录分片；缓存不能授予写权限。
        try {
            UploadTaskRepository upload_task_repository(m_db_client);
            const auto record_disposition = co_await upload_task_repository.RecordChunkIfInProgress(
                upload_id,
                user_id,
                chunk_index,
                write_result->size_bytes,
                write_result->md5_hash,
                write_result->object_key,
                write_result->etag
            );
            if (record_disposition == ChunkRecordDisposition::TaskRejected) {
                InvalidateUploadTaskCache(upload_id);
                Logger::Warn(log_context)
                    << "Late chunk rejected by upload task state: upload_id=" << upload_id
                    << ", chunk_index=" << chunk_index;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ResourceConflict, "Upload task no longer accepts chunks")
                );
            }
            if (record_disposition == ChunkRecordDisposition::MetadataConflict) {
                Logger::Warn(log_context)
                    << "Chunk metadata conflicts with existing upload progress: upload_id="
                    << upload_id << ", chunk_index=" << chunk_index;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ResourceConflict, "Chunk index already contains different content")
                );
            }

            disk::metrics::MetricsRegistry::GetInstance().RecordUploadChunk(
                write_result->size_bytes
            );

            Logger::HighVolumeSuccess(log_context)
                << "Chunk upload successful: upload_id=" << upload_id
                << ", chunk_index=" << chunk_index;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::HighVolumeSuccess(log_context)
                << "[upload_chunk] duration_us=" << duration_us
                << " outcome=success upload_id=" << upload_id
                << " chunk_index=" << chunk_index
                << " data_size=" << chunk_data.size();

            UploadChunkResponse response;
            response.chunk_index = chunk_index;
            response.uploaded = true;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to record chunk upload: " << e.base().what();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::HighVolumeFailure(log_context)
                << "[upload_chunk] duration_us=" << duration_us
                << " outcome=failure upload_id=" << upload_id
                << " chunk_index=" << chunk_index
                << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to record chunk upload")
            );
        }
    }

    /// ==================== CompleteUpload ====================

    auto UploadService::CompleteUpload(
        std::string upload_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<CompleteUploadResponse>> {
        log_context.operation = "upload_complete";
        if (upload_id.empty()) {
            log_context.upload_id.reset();
        } else {
            log_context.upload_id = upload_id;
        }
        log_context.job_id.reset();
        log_context.lease_owner.reset();
        log_context.state_version.reset();

        const auto config = ConfigMgr::GetInstance();
        disk::upload::UploadLifecycleService lifecycle_service(
            m_db_client,
            m_storage,
            m_upload_staging_storage,
            m_blob_store
        );
        auto lifecycle_result = co_await lifecycle_service.CompleteUpload(
            disk::upload::CompleteUploadCommand{ .upload_id = std::move(upload_id),
                                                 .user_id = user_id,
                                                 .lease_owner = config->GetInstanceId(),
                                                 .lease_duration_seconds = config->GetUploadFinalizeLeaseSeconds() },
            log_context
        );
        if (!lifecycle_result) {
            co_return std::unexpected(lifecycle_result.error());
        }

        const auto& invalidation = lifecycle_result->invalidation;
        for (const auto& upload_task_id : invalidation.upload_task_ids) {
            InvalidateUploadTaskCache(upload_task_id);
        }
        if (!invalidation.file_list_folder_ids.empty()) {
            co_await FileListCache::Invalidate(m_redis_service, user_id, log_context);
        }

        CompleteUploadResponse response{};
        if (lifecycle_result->file.has_value()) {
            const auto& file = lifecycle_result->file.value();
            response.file = FileItem{ .id = file.id,
                                      .name = file.name,
                                      .size = file.size,
                                      .hash = file.hash,
                                      .mime_type = file.mime_type,
                                      .parent_id = file.parent_id,
                                      .created_at = file.created_at };
        }

        co_return response;
    }

    auto UploadService::CancelUpload(
        std::string upload_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<uint64_t>> {
        log_context.operation = "upload_cancel";
        if (upload_id.empty()) {
            log_context.upload_id.reset();
        } else {
            log_context.upload_id = upload_id;
        }
        log_context.job_id.reset();
        log_context.lease_owner.reset();
        log_context.state_version.reset();

        Logger::Debug(log_context)
            << "Starting cancel upload: upload_id=" << upload_id << ", user_id=" << user_id;

        disk::upload::UploadLifecycleService lifecycle_service(
            m_db_client,
            m_storage,
            m_upload_staging_storage,
            m_blob_store
        );
        auto cancel_result = co_await lifecycle_service.CancelInProgressUpload(
            upload_id,
            user_id,
            log_context
        );
        if (!cancel_result) {
            Logger::Warn(log_context)
                << "Cancel upload lifecycle failed: upload_id=" << upload_id
                << ", error=" << cancel_result.error().message;
            co_return std::unexpected(cancel_result.error());
        }
        InvalidateUploadTaskCache(upload_id);

        log_context.state_version = cancel_result.value();
        Logger::Debug(log_context) << "Upload cancellation resolved: upload_id=" << upload_id;
        co_return cancel_result.value();
    }

    /// ==================== 私有辅助方法 ====================

    auto UploadService::FindUploadTask(
        const std::string& upload_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<UploadTasks>> {
        UploadTaskRepository upload_task_repository(m_db_client);
        auto task = co_await upload_task_repository.FindUnexpiredByIdForUser(upload_id, user_id);
        if (!task.has_value()) {
            Logger::Warn(log_context)
                << "Upload task not found or not owned by user: upload_id=" << upload_id
                << ", request_user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
        }

        co_return task.value();
    }

    auto UploadService::FindUploadStagingSession(
        const std::string& upload_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<storage::UploadStagingSession>> {
        try {
            UploadTaskRepository upload_task_repository(m_db_client);
            auto session = co_await upload_task_repository.FindStagingSessionForUser(
                upload_id,
                user_id
            );
            if (!session.has_value()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
            }
            co_return std::move(session.value());
        } catch (const std::exception& e) {
            Logger::Error(log_context)
                << "Failed to load upload staging session: upload_id=" << upload_id
                << ", error=" << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to load upload staging session")
            );
        }
    }

    auto UploadService::BuildUploadTaskCacheEntry(
        const UploadTasks& task,
        storage::UploadStagingSession staging_session
    ) -> UploadTaskCacheEntry {
        return UploadTaskCacheEntry{ .user_id = static_cast<uint64_t>(task.getValueOfUserId()),
                                     .file_size = static_cast<uint64_t>(task.getValueOfFileSize()),
                                     .chunk_size = static_cast<uint32_t>(task.getValueOfChunkSize()),
                                     .total_chunks = static_cast<uint32_t>(task.getValueOfTotalChunks()),
                                     .staging_session = std::move(staging_session),
                                     .cache_expires_at = std::chrono::steady_clock::now() + UPLOAD_TASK_CACHE_TTL };
    }

    auto UploadService::TryGetUploadTaskCacheEntry(const std::string& upload_id, uint64_t user_id)
        -> std::optional<UploadTaskCacheEntry> {

        const auto now = std::chrono::steady_clock::now();
        {
            /// 读路径使用共享锁，降低高频分片上传之间的锁竞争。
            std::shared_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
            auto it = m_upload_task_cache.find(upload_id);
            if (it == m_upload_task_cache.end()) {
                return std::nullopt;
            }

            if (it->second.cache_expires_at > now && it->second.user_id == user_id) {
                return it->second;
            }

            if (it->second.cache_expires_at > now) {
                return std::nullopt;
            }
        }

        /// 仅在确认缓存过期后切换到写锁做清理，避免读路径长时间持有独占锁。
        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        auto it = m_upload_task_cache.find(upload_id);
        if (it == m_upload_task_cache.end()) {
            return std::nullopt;
        }
        if (it->second.cache_expires_at <= now) {
            m_upload_task_cache.erase(it);
        }
        return std::nullopt;
    }

    auto UploadService::CacheUploadTaskEntry(const std::string& upload_id, UploadTaskCacheEntry entry)
        -> void {

        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        m_upload_task_cache[upload_id] = std::move(entry);
    }

    auto UploadService::InvalidateUploadTaskCache(const std::string& upload_id) -> void {
        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        m_upload_task_cache.erase(upload_id);
    }

    auto UploadService::StartUploadTaskCacheMaintenance() -> void {
        if (auto* loop = drogon::app().getLoop(); loop != nullptr) {
            loop->runEvery(
                UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS,
                [this]() { EvictExpiredUploadTaskCacheEntries(); }
            );
            Logger::Debug(UploadRuntimeLogContext())
                << "Upload task cache maintenance timer started: interval_seconds="
                << UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS;
        }
    }

    auto UploadService::EvictExpiredUploadTaskCacheEntries() -> void {
        const auto now = std::chrono::steady_clock::now();
        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        for (auto it = m_upload_task_cache.begin(); it != m_upload_task_cache.end();) {
            if (it->second.cache_expires_at <= now) {
                it = m_upload_task_cache.erase(it);
            } else {
                ++it;
            }
        }
    }

} // namespace disk::file
