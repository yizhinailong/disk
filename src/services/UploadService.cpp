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

#include "services/UploadTaskRepository.hpp"
#include "models/UploadTasks.hpp"
#include "storage/IFileStorage.hpp"
#include "services/UploadLifecycleService.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::file {

    using disk::utils::ConfigMgr;
    using disk::utils::FileHashUtil;
    using drogon_model::disk::UploadTasks;

    /// ==================== 构造函数 ====================

    UploadService::UploadService(drogon::orm::DbClientPtr db_client, storage::IFileStorage* storage)
        : m_db_client(std::move(db_client)), m_storage(storage) {
        StartUploadTaskCacheMaintenance();
        Logger::Debug() << "UploadService initialization completed";
    }

    /// ==================== InitUpload ====================

    auto UploadService::InitUpload(InitUploadRequest request, uint64_t user_id)
        -> drogon::Task<Result<InitUploadResponse>> {

        Logger::Debug() << "Starting initialize upload: filename=\"" << request.filename
                  << "\", file_size=" << request.file_size << ", file_hash=" << request.file_hash
                  << ", parent_id=" << request.parent_id << ", user_id=" << user_id;

        auto config = ConfigMgr::GetInstance();
        disk::upload::UploadLifecycleService lifecycle_service(m_db_client, m_storage);
        auto lifecycle_result = co_await lifecycle_service.InitializeUpload(
            disk::upload::InitUploadCommand{ .filename = std::move(request.filename),
                                             .file_size = request.file_size,
                                             .file_hash = std::move(request.file_hash),
                                             .parent_id = request.parent_id,
                                             .user_id = user_id,
                                             .max_file_size = config->GetMaxFileSize(),
                                             .chunk_size = config->GetChunkSize(),
                                             .expiry_seconds = config->GetUploadTaskExpirySeconds() }
        );
        if (!lifecycle_result) {
            co_return std::unexpected(lifecycle_result.error());
        }

        const auto& invalidation = lifecycle_result->invalidation;
        for (const auto& upload_task_id : invalidation.upload_task_ids) {
            InvalidateUploadTaskCache(upload_task_id);
        }
        co_await InvalidateFileListCache(user_id, invalidation.file_list_folder_ids);

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
        uint64_t user_id
    ) -> drogon::Task<Result<UploadChunkResponse>> {

        auto start = std::chrono::steady_clock::now();

        Logger::Debug() << "Starting upload chunk: upload_id=" << upload_id
                  << ", chunk_index=" << chunk_index << ", chunk_hash=" << chunk_hash
                  << ", data_size=" << chunk_data.size();

        /// 1. 优先读取短 TTL 上传任务缓存，命中后避免重复查询数据库
        auto cached_task = TryGetUploadTaskCacheEntry(upload_id, user_id);
        if (!cached_task.has_value()) {
            auto task_result = co_await FindUploadTask(upload_id, user_id);
            if (!task_result) {
                Logger::Warn() << "Upload task verification failed: " << upload_id;

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                         << " outcome=failure upload_id=" << upload_id
                         << " chunk_index=" << chunk_index
                         << " data_size=" << chunk_data.size();

                co_return std::unexpected(task_result.error());
            }

            auto cache_entry = BuildUploadTaskCacheEntry(task_result.value());
            CacheUploadTaskEntry(upload_id, cache_entry);
            cached_task = std::move(cache_entry);
        }

        const auto& task = cached_task.value();

        /// 2. 验证任务未过期
        if (disk::upload::IsExpired(task.expires_at, trantor::Date::now())) {
            Logger::Warn() << "Upload task expired: " << upload_id;
            InvalidateUploadTaskCache(upload_id);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::UploadTaskNotFound, "Upload task expired")
            );
        }

        /// 3. 验证分片索引和大小符合任务几何信息
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
                Logger::Warn() << "Chunk index out of range: chunk_index=" << chunk_index
                         << ", total_chunks=" << task.total_chunks;
            } else {
                Logger::Warn() << "Unexpected chunk size: upload_id=" << upload_id
                         << ", chunk_index=" << chunk_index
                         << ", expected_size=" << acceptance_error.expected_size
                         << ", actual_size=" << chunk_data.size()
                         << ", file_size=" << task.file_size
                         << ", chunk_size=" << task.chunk_size;
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(acceptance_error.error);
        }

        /// 5. 将请求体复制到拥有所有权的缓冲区，只做一次哈希+落盘复用。
        std::string chunk_payload{ chunk_data };
        auto actual_hash = FileHashUtil::HashMd5(chunk_payload);
        if (actual_hash != chunk_hash) {
            Logger::Warn() << "Chunk hash mismatch: expected=" << chunk_hash
                     << ", actual=" << actual_hash;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Chunk hash mismatch")
            );
        }

        /// 6. 创建临时目录并写入分片
        auto write_result = co_await m_storage->WriteChunk(upload_id, chunk_index, std::move(chunk_payload));
        if (!write_result) {
            Logger::Error() << "Failed to write chunk file: upload_id=" << upload_id
                      << ", chunk_index=" << chunk_index << ", error="
                      << static_cast<int>(write_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(write_result.error());
        }

        /// 7. 记录已上传分片（幂等：ON CONFLICT DO NOTHING 允许重复上传同一分片）
        try {
            UploadTaskRepository upload_task_repository(m_db_client);
            co_await upload_task_repository.RecordChunkUploadedIfAbsent(upload_id, chunk_index);

            Logger::Debug() << "Chunk upload successful: upload_id=" << upload_id
                      << ", chunk_index=" << chunk_index;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Debug() << "[upload_chunk] duration_us=" << duration_us
                      << " outcome=success upload_id=" << upload_id
                      << " chunk_index=" << chunk_index
                      << " data_size=" << chunk_data.size();

            UploadChunkResponse response;
            response.chunk_index = chunk_index;
            response.uploaded = true;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to record chunk upload: " << e.base().what();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to record chunk upload")
            );
        }
    }

    /// ==================== CompleteUpload ====================

    auto UploadService::CompleteUpload(std::string upload_id, uint64_t user_id)
        -> drogon::Task<Result<CompleteUploadResponse>> {

        disk::upload::UploadLifecycleService lifecycle_service(m_db_client, m_storage);
        auto lifecycle_result = co_await lifecycle_service.CompleteUpload(
            disk::upload::CompleteUploadCommand{ .upload_id = std::move(upload_id),
                                                 .user_id = user_id }
        );
        if (!lifecycle_result) {
            co_return std::unexpected(lifecycle_result.error());
        }

        const auto& invalidation = lifecycle_result->invalidation;
        for (const auto& upload_task_id : invalidation.upload_task_ids) {
            InvalidateUploadTaskCache(upload_task_id);
        }
        co_await InvalidateFileListCache(user_id, invalidation.file_list_folder_ids);

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

    auto UploadService::CancelUpload(std::string upload_id, uint64_t user_id)
        -> drogon::Task<Result<void>> {

        Logger::Debug() << "Starting cancel upload: upload_id=" << upload_id << ", user_id=" << user_id;

        /// 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            Logger::Warn() << "Upload task verification failed: " << upload_id;
            co_return std::unexpected(task_result.error());
        }
        const auto task = task_result.value();

        /// 2. Check idempotency: already in terminal state
        if (!disk::upload::CanCancelOrExpire(task.getValueOfStatus())) {
            Logger::Debug() << "Upload task already in terminal state: upload_id=" << upload_id
                      << ", status=" << task.getValueOfStatus();
            co_return {};
        }

        disk::upload::UploadLifecycleService lifecycle_service(m_db_client, m_storage);
        auto cancel_result = co_await lifecycle_service.CancelInProgressUpload(
            upload_id,
            user_id,
            task.getValueOfReservedBytes()
        );
        if (!cancel_result) {
            co_return std::unexpected(cancel_result.error());
        }
        InvalidateUploadTaskCache(upload_id);

        Logger::Debug() << "Upload task cancelled: upload_id=" << upload_id;
        co_return {};
    }

    /// ==================== 私有辅助方法 ====================

    auto UploadService::FindUploadTask(const std::string& upload_id, uint64_t user_id) const
        -> drogon::Task<Result<UploadTasks>> {
        UploadTaskRepository upload_task_repository(m_db_client);
        auto task = co_await upload_task_repository.FindByIdForUser(upload_id, user_id);
        if (!task.has_value()) {
            Logger::Warn() << "Upload task not found or not owned by user: upload_id=" << upload_id
                     << ", request_user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
        }

        co_return task.value();
    }

    auto UploadService::BuildUploadTaskCacheEntry(const UploadTasks& task) -> UploadTaskCacheEntry {
        return UploadTaskCacheEntry{ .user_id = task.getValueOfUserId(),
                                     .file_size = task.getValueOfFileSize(),
                                     .chunk_size = task.getValueOfChunkSize(),
                                     .total_chunks = task.getValueOfTotalChunks(),
                                     .expires_at = task.getValueOfExpiresAt(),
                                     .status = task.getValueOfStatus(),
                                     .file_hash = task.getValueOfFileHash(),
                                     .filename = task.getValueOfFilename(),
                                     .parent_id = task.getValueOfFolderId(),
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
            Logger::Debug() << "Upload task cache maintenance timer started (interval="
                      << UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS << "s)";
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

    auto UploadService::InvalidateFileListCache(uint64_t user_id, const std::vector<uint64_t>& folder_ids)
        -> drogon::Task<void> {
        for (const auto folder_id : folder_ids) {
            const auto prefix = disk::redis::RedisKeyPrefix::BuildFileListCachePrefix(user_id, folder_id);
            auto delete_result = co_await m_redis_service->DeleteByPrefix(prefix);
            if (!delete_result) {
                Logger::Warn() << "Failed to invalidate file list cache by prefix: " << prefix;
            }
        }
    }

} ///< namespace disk::file
