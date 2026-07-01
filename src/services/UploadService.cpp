/**
 * @file UploadService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadService.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>

#include <sstream>

#include <json/reader.h>
#include <json/writer.h>

#include "FileServiceUtils.hpp"
#include "services/ContentService.hpp"
#include "services/QuotaService.hpp"
#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/UploadTaskChunks.hpp"
#include "storage/IFileStorage.hpp"
#include "services/UploadLifecycleService.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/StageTimer.hpp"

namespace disk::file {

    using disk::utils::ConfigMgr;
    using disk::utils::FileHashUtil;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::UploadTaskChunks;
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
        auto max_file_size = config->GetMaxFileSize();
        if (request.file_size > max_file_size) {
            Logger::Warn() << "Upload file exceeds max size: filename=\"" << request.filename
                     << "\", file_size=" << request.file_size
                     << ", max_file_size=" << max_file_size << ", user_id=" << user_id;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "File size exceeds maximum allowed size")
            );
        }

        /// 1. Compound pre-check: folder location + filename collision + instant upload + resume
        auto combined = co_await m_db_client->execSqlCoro(
            "WITH folder_loc AS ("
            "  SELECT path, depth FROM folders WHERE id = $1 AND user_id = $2"
            "), filename_exists AS ("
            "  SELECT COUNT(*) AS cnt FROM files"
            "  WHERE user_id = $2 AND folder_id = $1 AND name = $3"
            "), existing_task AS ("
            "  SELECT id FROM upload_tasks"
            "  WHERE user_id = $2 AND file_hash = $4 AND status = $5 LIMIT 1"
            ")"
            " SELECT"
            "   (SELECT path FROM folder_loc) AS folder_path,"
            "   (SELECT depth FROM folder_loc) AS folder_depth,"
            "   (SELECT cnt FROM filename_exists) AS filename_count,"
            "   (SELECT id FROM existing_task) AS task_id",
            request.parent_id,
            user_id,
            request.filename,
            request.file_hash,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );

        const auto& row = combined[0];

        /// Validate folder location
        if (request.parent_id != 0) {
            if (row["folder_path"].isNull()) {
                Logger::Warn() << "Folder not found: parent_id=" << request.parent_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        /// Check filename collision
        auto filename_count = row["filename_count"].as<int64_t>();
        if (filename_count > 0) {
            Logger::Warn() << "File with same name already exists during upload init: "
                     << request.filename << ", parent_id=" << request.parent_id
                     << ", user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
        }

        /// Detect instant upload / resume / new upload decision
        disk::content::ContentService content_service(m_db_client);
        auto existing_content = co_await content_service.FindByMd5(request.file_hash);
        auto existing_task_id = row["task_id"].isNull() ? std::string{} : row["task_id"].as<std::string>();
        auto init_decision = disk::upload::DecideInitFlow(existing_content.has_value(), existing_task_id);

        if (init_decision.type == disk::upload::InitDecisionType::InstantUpload) {
            auto content_id = existing_content->id;
            const auto& content_mime_type = existing_content->mime_type;
            Logger::Debug() << "Instant upload check successful: file_hash=" << request.file_hash
                      << ", content_id=" << content_id;

            std::shared_ptr<drogon::orm::Transaction> transaction;
            try {
                transaction = co_await m_db_client->newTransactionCoro();

                /// 在事务内检查同名文件
                if (co_await IsFilenameExists(
                        transaction,
                        request.parent_id,
                        request.filename,
                        user_id
                    )) {
                    Logger::Warn() << "File with same name already exists: " << request.filename;
                    co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
                }

                /// 事务内递增引用计数
                disk::content::ContentService content_service(m_db_client);
                auto increment_result = co_await content_service.IncrementRefCount(transaction, content_id);
                if (!increment_result) {
                    Logger::Warn() << "File content not found for instant upload: content_id="
                             << content_id;
                    throw std::runtime_error("Failed to increment file content reference count");
                }

                auto parent_location_result =
                    co_await utils::ResolveFolderLocation(transaction, request.parent_id, user_id);
                if (!parent_location_result) {
                    co_return std::unexpected(parent_location_result.error());
                }

                /// 创建文件记录（使用 compound query 提供的 mime_type，无需二次读取）
                Files file;
                file.setUserId(user_id);
                file.setContentId(content_id);
                file.setFolderId(request.parent_id);
                file.setName(request.filename);
                file.setExtension(ExtractExtension(request.filename));
                file.setSize(request.file_size);
                file.setMimeType(content_mime_type);
                file.setPath(utils::BuildFilePath(parent_location_result->path, request.filename));
                file.setIsFavorite(0);
                file.setDownloadCount(0);

                CoroMapper<Files> file_mapper(transaction);
                file = co_await file_mapper.insert(file);

                /// 注：秒传时 storage_used 不增加，因为物理文件已存在

                /// 构造响应
                InitUploadResponse response;
                response.instant_upload = true;
                response.file =
                    FileItem{ .id = file.getValueOfId(),
                              .name = file.getValueOfName(),
                              .size = file.getValueOfSize(),
                              .hash = request.file_hash,
                              .mime_type = file.getValueOfMimeType(),
                              .parent_id = file.getValueOfFolderId(),
                              .created_at = file.getValueOfCreatedAt().toDbStringLocal() };

                Logger::Debug() << "Instant upload completed: file_id=" << file.getValueOfId();

                co_await InvalidateFileListCache(user_id, {request.parent_id});
                co_return response;

            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error() << "Instant upload create file record failed: " << e.base().what();
                if (transaction) {
                    try {
                        transaction->rollback();
                    } catch (const std::exception& rollback_e) {
                        Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create file record")
                );
            } catch (const std::exception& e) {
                Logger::Error() << "Instant upload create file record failed: " << e.what();
                if (transaction) {
                    try {
                        transaction->rollback();
                    } catch (const std::exception& rollback_e) {
                        Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create file record")
                );
            }
        }

        /// 2. 检测断点续传
        if (init_decision.type == disk::upload::InitDecisionType::ResumeUpload) {
            auto existing_task = co_await FindExistingTask(user_id, request.file_hash);
            if (existing_task.has_value()) {
                const auto& task = existing_task.value();
                const auto& task_id = task.getValueOfId();

                if (disk::upload::IsExpired(task.getValueOfExpiresAt(), trantor::Date::now())) {
                    Logger::Info() << "Expired upload task found, discarding: upload_id=" << task_id;
                    InvalidateUploadTaskCache(task_id);

                    try {
                        co_await m_db_client->execSqlCoro(
                            "DELETE FROM upload_tasks WHERE id = $1 AND status = $2",
                            task_id,
                            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
                        );
                    } catch (const drogon::orm::DrogonDbException& e) {
                        Logger::Warn() << "Failed to delete expired upload task: " << e.base().what();
                    }

                    auto cleanup_result = co_await m_storage->CleanupTemp(task_id);
                    if (!cleanup_result) {
                        Logger::Warn() << "Failed to cleanup temp for expired task: upload_id=" << task_id;
                    }
                } else {
                    Logger::Debug() << "Resume upload check successful: upload_id=" << task_id;
                    InvalidateUploadTaskCache(task_id);

                    auto chunk_result = co_await m_db_client->execSqlCoro(
                        "SELECT chunk_index FROM upload_task_chunks WHERE task_id = $1 ORDER BY chunk_index",
                        task_id
                    );

                    InitUploadResponse response;
                    response.upload_id = task_id;
                    response.chunk_size = task.getValueOfChunkSize();
                    response.total_chunks = task.getValueOfTotalChunks();
                    response.instant_upload = false;

                    response.uploaded_chunks.clear();
                    for (const auto& chunk_row : chunk_result) {
                        response.uploaded_chunks.push_back(chunk_row["chunk_index"].as<uint32_t>());
                    }

                    co_return response;
                }
            }
        }

        /// 3. 预留存储配额
        auto quota_result = co_await ReserveStorageQuota(user_id, request.file_size);
        if (!quota_result) {
            Logger::Warn() << "Storage quota reservation failed: user_id=" << user_id;
            co_return std::unexpected(quota_result.error());
        }

        /// 4. 创建新的上传任务
        auto chunk_size = config->GetChunkSize();
        if (chunk_size == 0) {
            Logger::Error() << "Invalid upload chunk size configured: 0";
            co_await ReleaseReservedQuota(user_id, request.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Invalid upload chunk size configuration")
            );
        }

        const auto total_chunks_u64 = ((request.file_size - 1) / chunk_size) + 1;
        if (total_chunks_u64 > std::numeric_limits<uint32_t>::max()) {
            Logger::Warn() << "Upload requires too many chunks: filename=\"" << request.filename
                     << "\", file_size=" << request.file_size
                     << ", chunk_size=" << chunk_size
                     << ", total_chunks=" << total_chunks_u64;
            co_await ReleaseReservedQuota(user_id, request.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "File requires too many chunks")
            );
        }
        auto total_chunks = static_cast<uint32_t>(total_chunks_u64);
        auto expiry_seconds = config->GetUploadTaskExpirySeconds();

        /// 生成上传 ID
        auto upload_id = drogon::utils::getUuid();

        UploadTasks task;
        task.setId(upload_id);
        task.setUserId(user_id);
        task.setFolderId(request.parent_id);
        task.setFilename(request.filename);
        task.setFileSize(request.file_size);
        task.setFileHash(request.file_hash);
        task.setChunkSize(chunk_size);
        task.setTotalChunks(total_chunks);
        task.setReservedBytes(request.file_size);
        task.setTempPath(upload_id);
        task.setStatus(disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)); ///< 进行中
        task.setExpiresAt(trantor::Date::now().after(expiry_seconds));

        bool create_task_failed = false;
        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            task = co_await mapper.insert(task);

            Logger::Debug() << "Upload task created successfully: upload_id=" << task.getValueOfId()
                      << ", total_chunks=" << total_chunks;
            InvalidateUploadTaskCache(task.getValueOfId());

            /// 预创建临时上传目录，避免每个分片写入时重复创建
            auto ensure_result = co_await m_storage->EnsureUploadTempDir(task.getValueOfId());
            if (!ensure_result) {
                Logger::Warn() << "Failed to ensure upload temp directory: upload_id="
                         << task.getValueOfId();
            }

            InitUploadResponse response;
            response.upload_id = task.getValueOfId();
            response.chunk_size = task.getValueOfChunkSize();
            response.total_chunks = task.getValueOfTotalChunks();
            response.uploaded_chunks = {};
            response.instant_upload = false;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to create upload task: " << e.base().what();
            create_task_failed = true;
        }

        if (create_task_failed) {
            co_await ReleaseReservedQuota(user_id, request.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create upload task")
            );
        }

        co_return std::unexpected(
            ErrorInfo(ErrorCode::InternalError, "Unexpected upload initialization state")
        );
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

        /// 7. 记录已上传分片（幂等：INSERT IGNORE 允许重复上传同一分片）
        try {
            co_await m_db_client->execSqlCoro(
                "INSERT INTO upload_task_chunks (task_id, chunk_index, uploaded_at) VALUES ($1, $2, NOW()) ON CONFLICT DO NOTHING",
                upload_id,
                chunk_index
            );

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

        auto start = std::chrono::steady_clock::now();

        Logger::Debug() << "Starting complete upload: upload_id=" << upload_id << ", user_id=" << user_id;

        /// 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            Logger::Warn() << "Upload task verification failed: " << upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id;

            co_return std::unexpected(task_result.error());
        }

        auto task = task_result.value();

        /// 2. Check idempotency: already completed
        if (task.getValueOfStatus() == disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Completed)) {
            Logger::Debug() << "Upload task already completed: upload_id=" << upload_id;
            InvalidateUploadTaskCache(upload_id);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Debug() << "[complete_upload] duration_us=" << duration_us
                      << " outcome=success upload_id=" << upload_id
                      << " total_chunks=" << task.getValueOfTotalChunks();

            co_return CompleteUploadResponse{};
        }

        /// 3. 单次聚合查询校验分片完整性
        auto chunk_scan_start = std::chrono::steady_clock::now();
        const auto LogChunkScanDuration = [&chunk_scan_start, &upload_id]() {
            Logger::Debug() << "[stage_timer] chunk_scan duration_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - chunk_scan_start
                         )
                             .count()
                      << " upload_id=" << upload_id;
        };

        auto coverage_result = co_await GetUploadedChunkCoverage(m_db_client, upload_id);
        if (!coverage_result.has_value()) {
            LogChunkScanDuration();
            Logger::Error() << "Failed to query chunk coverage: upload_id=" << upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to query chunk coverage")
            );
        }

        const auto& coverage = coverage_result.value();
        const auto total_chunks = task.getValueOfTotalChunks();
        const auto chunks_valid = disk::upload::IsCompleteCoverage(
            total_chunks,
            disk::upload::ChunkCoverage{ .uploaded_count = coverage.uploaded_count,
                                         .max_chunk_index = coverage.max_chunk_index }
        );

        LogChunkScanDuration();

        if (!chunks_valid) {
            Logger::Warn() << "Not all chunks uploaded: uploaded=" << coverage.uploaded_count
                     << ", total=" << total_chunks
                     << ", max_index=" << coverage.max_chunk_index;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << total_chunks;

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
            );
        }

        /// 3. 组装分片（同时计算 MD5 + SHA256）
        auto assemble_start = std::chrono::steady_clock::now();
        auto assemble_result = co_await m_storage->AssembleChunks(upload_id, task.getValueOfTotalChunks());
        Logger::Debug() << "[stage_timer] assemble duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - assemble_start
                     )
                         .count()
                  << " upload_id=" << upload_id
                  << " total_chunks=" << task.getValueOfTotalChunks();
        if (!assemble_result) {
            Logger::Error() << "Failed to assemble chunks: upload_id=" << upload_id
                      << ", error=" << static_cast<int>(assemble_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(assemble_result.error());
        }
        const auto& assembled = assemble_result.value();
        const auto& assemble_path = assembled.path;

        /// 4. 使用组装时计算的哈希值
        const auto& final_hash = assembled.md5_hash;
        const auto& precomputed_sha256 = assembled.sha256_hash;
        if (final_hash != task.getValueOfFileHash()) {
            Logger::Error() << "File hash mismatch: expected=" << task.getValueOfFileHash()
                      << ", actual=" << final_hash;
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file after hash mismatch: "
                         << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

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
        auto lookup_result = co_await [this,
                                       &final_hash,
                                       &task,
                                       user_id]() -> drogon::Task<FinalizeLookupResult> {
            FinalizeLookupResult lookup;
            disk::content::ContentService content_service(m_db_client);
            auto existing_content = co_await content_service.FindByMd5(final_hash);
            if (existing_content.has_value()) {
                lookup.existing_content_id = existing_content->id;
            }

            try {
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT EXISTS(SELECT 1 FROM files WHERE user_id = $1 AND folder_id = $2 AND name = $3) AS filename_exists",
                    user_id,
                    task.getValueOfFolderId(),
                    task.getValueOfFilename()
                );

                if (!result.empty()) {
                    lookup.filename_exists = result[0]["filename_exists"].as<int>() != 0;
                }

                co_return lookup;
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error() << "Failed to query finalize upload metadata: " << e.base().what();
                co_return lookup;
            }
        }();

        if (lookup_result.filename_exists) {
            Logger::Warn() << "File with same name already exists: " << task.getValueOfFilename();
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file on duplicate name: "
                         << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            Logger::Info() << "[stage_timer] dedup_lookup duration_ms="
                     << std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - dedup_start
                        )
                            .count()
                     << " upload_id=" << upload_id
                     << " dedup_hit=" << (lookup_result.existing_content_id.has_value() ? "true" : "false")
                     << " filename_exists=true";

            co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
        }

        auto existing_content = lookup_result.existing_content_id;
        auto finalize_storage_decision = disk::upload::DecideFinalizeStorage(existing_content);
        std::filesystem::path final_storage_path;
        std::string final_sha256;
        bool should_compensate_storage_file = false;

        if (finalize_storage_decision.type == disk::upload::FinalizeStorageDecisionType::ReuseExistingContent) {
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file after dedup: "
                         << static_cast<int>(delete_result.error().code);
            }
            Logger::Debug() << "File dedup successful: content_id="
                      << finalize_storage_decision.existing_content_id.value();
        } else {
            auto promote_result = co_await m_storage->PromoteToFinal(assemble_path, final_hash);
            if (!promote_result) {
                Logger::Error() << "Failed to move file to final storage: error="
                          << static_cast<int>(promote_result.error().code);
                auto cleanup_result = co_await m_storage->DeletePath(assemble_path);
                if (!cleanup_result) {
                    Logger::Warn() << "Failed to cleanup assemble file after promote failure: "
                             << static_cast<int>(cleanup_result.error().code);
                }

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                Logger::Info() << "[complete_upload] duration_us=" << duration_us
                         << " outcome=failure upload_id=" << upload_id
                         << " total_chunks=" << task.getValueOfTotalChunks();

                co_return std::unexpected(promote_result.error());
            }

            final_storage_path = promote_result.value();
            final_sha256 = precomputed_sha256;
            should_compensate_storage_file = true;
        }
        Logger::Debug() << "[stage_timer] dedup_lookup duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - dedup_start
                     )
                         .count()
                  << " upload_id=" << upload_id
                  << " dedup_hit=" << (existing_content.has_value() ? "true" : "false")
                  << " filename_exists=false";

        std::shared_ptr<drogon::orm::Transaction> transaction;
        Files file;
        bool db_operation_failed = false;
        auto tx_start = std::chrono::steady_clock::now();
        try {
            transaction = co_await m_db_client->newTransactionCoro();

            CoroMapper<Files> file_mapper(transaction);
            disk::content::ContentService content_service(m_db_client);

            uint64_t content_id = 0;
            if (existing_content.has_value()) {
                content_id = existing_content.value();
                auto increment_result = co_await content_service.IncrementRefCount(transaction, content_id);
                if (!increment_result) {
                    Logger::Warn() << "File content not found when finalizing upload: content_id="
                             << content_id;
                    throw std::runtime_error("Failed to increment file content reference count");
                }
            } else {
                auto content = co_await content_service.Create(
                    transaction,
                    disk::content::NewContent{ .hash_md5 = final_hash,
                                               .hash_sha256 = final_sha256,
                                               .size = task.getValueOfFileSize(),
                                               .storage_path = final_storage_path.string(),
                                               .mime_type = "" }
                );
                content_id = content.id;
                Logger::Debug() << "FileContents created successfully: content_id=" << content_id;
            }

            auto parent_location_result =
                co_await utils::ResolveFolderLocation(transaction, task.getValueOfFolderId(), user_id);
            if (!parent_location_result) {
                throw std::runtime_error("Target upload folder not found");
            }

            file.setUserId(user_id);
            file.setContentId(content_id);
            file.setFolderId(task.getValueOfFolderId());
            file.setName(task.getValueOfFilename());
            file.setExtension(ExtractExtension(task.getValueOfFilename()));
            file.setSize(task.getValueOfFileSize());
            file.setMimeType("");
            file.setPath(utils::BuildFilePath(parent_location_result->path, task.getValueOfFilename()));
            file.setIsFavorite(0);
            file.setDownloadCount(0);

            file = co_await file_mapper.insert(file);

            disk::quota::QuotaService quota_service(m_db_client);
            auto transfer_result = co_await quota_service.CommitReservedToUsed(
                transaction,
                user_id,
                task.getValueOfFileSize()
            );
            if (!transfer_result) {
                throw std::runtime_error(transfer_result.error().message);
            }

            auto finalize_result = co_await transaction->execSqlCoro(
                "UPDATE upload_tasks SET status = $1, finalized_at = NOW() WHERE id = $2 AND status = $3",
                disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Completed),
                upload_id,
                disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
            );
            if (finalize_result.affectedRows() == 0) {
                throw std::runtime_error("Failed to finalize upload task");
            }

            co_await transaction->execSqlCoro(
                "DELETE FROM upload_task_chunks WHERE task_id = $1",
                upload_id
            );

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Database operation failed: " << e.base().what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            db_operation_failed = true;
        } catch (const std::exception& e) {
            Logger::Error() << "Database operation failed: " << e.what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            db_operation_failed = true;
        }
        Logger::Debug() << "[stage_timer] tx duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - tx_start
                     )
                         .count()
                  << " upload_id=" << upload_id
                  << " success=" << (!db_operation_failed ? "true" : "false");

        if (db_operation_failed) {
            auto compensation_start = std::chrono::steady_clock::now();
            if (should_compensate_storage_file) {
                auto cleanup_result = co_await m_storage->DeletePath(final_storage_path);
                if (!cleanup_result) {
                    Logger::Error() << "Compensation failed, orphan storage file may remain: "
                              << final_storage_path;
                }
            }
            Logger::Info() << "[stage_timer] compensation_cleanup duration_ms="
                     << std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - compensation_start
                        )
                            .count()
                     << " upload_id=" << upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Database operation failed")
            );
        }

        Logger::Debug() << "Files record created successfully: file_id=" << file.getValueOfId();
        InvalidateUploadTaskCache(upload_id);

        /// 7. Cleanup temp directory
        auto temp_cleanup_start = std::chrono::steady_clock::now();
        auto cleanup_result = co_await m_storage->CleanupTemp(upload_id);
        Logger::Debug() << "[stage_timer] temp_cleanup duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - temp_cleanup_start
                     )
                         .count()
                  << " upload_id=" << upload_id;
        if (!cleanup_result) {
            Logger::Warn() << "Failed to cleanup temp artifacts: "
                     << static_cast<int>(cleanup_result.error().code);
        }

        CompleteUploadResponse response;
        response.file = FileItem{ .id = file.getValueOfId(),
                                  .name = file.getValueOfName(),
                                  .size = file.getValueOfSize(),
                                  .hash = final_hash,
                                  .mime_type = file.getValueOfMimeType(),
                                  .parent_id = file.getValueOfFolderId(),
                                  .created_at = file.getValueOfCreatedAt().toDbStringLocal() };

        Logger::Debug() << "File upload completed: file_id=" << file.getValueOfId()
                  << ", filename=" << task.getValueOfFilename();

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        Logger::Debug() << "[complete_upload] duration_us=" << duration_us
                  << " outcome=success upload_id=" << upload_id
                  << " total_chunks=" << task.getValueOfTotalChunks();

        co_await InvalidateFileListCache(user_id, {file.getValueOfFolderId()});
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

    auto UploadService::CheckStorageQuota(uint64_t user_id, uint64_t file_size) const
        -> drogon::Task<Result<void>> {
        disk::quota::QuotaService quota_service(m_db_client);
        co_return co_await quota_service.ConsumeUsedStorage(m_db_client, user_id, file_size);
    }

    auto UploadService::ReserveStorageQuota(uint64_t user_id, uint64_t file_size) const
        -> drogon::Task<Result<void>> {

        disk::quota::QuotaService quota_service(m_db_client);
        co_return co_await quota_service.ReserveUploadStorage(m_db_client, user_id, file_size);
    }

    auto UploadService::ReleaseReservedQuota(uint64_t user_id, uint64_t reserved_bytes)
        -> drogon::Task<void> {

        disk::quota::QuotaService quota_service(m_db_client);
        co_await quota_service.ReleaseReservedStorage(m_db_client, user_id, reserved_bytes);
    }

    auto UploadService::FindExistingTask(uint64_t user_id, const std::string& file_hash) const
        -> drogon::Task<std::optional<UploadTasks>> {

        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            auto task = co_await mapper.findOne(
                Criteria(UploadTasks::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(UploadTasks::Cols::_file_hash, CompareOperator::EQ, file_hash) &&
                Criteria(UploadTasks::Cols::_status, CompareOperator::EQ, disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress))
            );

            co_return task;

        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
    }

    auto UploadService::FindUploadTask(const std::string& upload_id, uint64_t user_id) const
        -> drogon::Task<Result<UploadTasks>> {

        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            auto task = co_await mapper.findByPrimaryKey(upload_id);

            if (task.getValueOfUserId() != user_id) {
                Logger::Warn() << "Upload task does not belong to current user: upload_id=" << upload_id
                         << ", task_user_id=" << task.getValueOfUserId()
                         << ", request_user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
            }

            co_return task;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to query upload task: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
        }
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

    auto UploadService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {
        co_await UpdateStorageUsed(m_db_client, user_id, delta);
    }

    auto UploadService::IsFilenameExists(
        uint64_t folder_id,
        const std::string& filename,
        uint64_t user_id
    ) const -> drogon::Task<bool> {

        try {
            CoroMapper<Files> mapper(m_db_client);
            auto count = co_await mapper.count(
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(Files::Cols::_folder_id, CompareOperator::EQ, folder_id) &&
                Criteria(Files::Cols::_name, CompareOperator::EQ, filename)
            );

            co_return count > 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to check filename: " << e.base().what();
            co_return false;
        }
    }

    auto UploadService::ExtractExtension(const std::string& filename) -> std::string {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == filename.length() - 1) {
            return "";
        }
        return filename.substr(pos + 1);
    }

    auto UploadService::IsImageMimeType(const std::string& mime_type) -> bool {
        return mime_type.starts_with("image/");
    }

    /// ── 事务感知辅助方法实现 ──

    auto UploadService::CheckStorageQuota(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t file_size
    ) const -> drogon::Task<Result<void>> {
        disk::quota::QuotaService quota_service(m_db_client);
        co_return co_await quota_service.ConsumeUsedStorage(client, user_id, file_size);
    }

    auto UploadService::UpdateStorageUsed(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        int64_t delta
    ) -> drogon::Task<void> {

        disk::quota::QuotaService quota_service(m_db_client);
        co_await quota_service.AdjustUsedStorage(client, user_id, delta);
    }

    auto UploadService::LookupExistingContentMetadata(
        const drogon::orm::DbClientPtr& client,
        const std::string& file_hash
    ) const -> drogon::Task<std::optional<ExistingContentMetadata>> {

        try {
            auto result = co_await client->execSqlCoro(
                "SELECT id, mime_type FROM file_contents WHERE hash_md5 = $1 LIMIT 1",
                file_hash
            );

            if (result.empty()) {
                co_return std::nullopt;
            }

            ExistingContentMetadata metadata;
            metadata.id = result[0]["id"].as<uint64_t>();
            metadata.mime_type = result[0]["mime_type"].as<std::string>();
            co_return metadata;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to lookup existing content metadata: " << e.base().what();
            co_return std::nullopt;
        }
    }

    auto UploadService::IsFilenameExists(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        const std::string& filename,
        uint64_t user_id
    ) const -> drogon::Task<bool> {

        try {
            auto result = co_await client->execSqlCoro(
                "SELECT COUNT(*) AS cnt FROM files " "WHERE user_id = $1 AND folder_id = $2 AND name = $3",
                user_id,
                folder_id,
                filename
            );

            if (!result.empty()) {
                co_return result[0]["cnt"].as<uint64_t>() > 0;
            }
            co_return false;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to check filename (transaction): " << e.base().what();
            co_return false;
        }
    }

    auto UploadService::GetUploadedChunkCoverage(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id
    ) const -> drogon::Task<std::optional<UploadedChunkCoverage>> {

        try {
            auto result = co_await client->execSqlCoro(
                "SELECT COUNT(*) AS uploaded_count, " "COALESCE(MAX(chunk_index), -1) AS max_chunk_index " "FROM upload_task_chunks WHERE task_id = $1",
                upload_id
            );

            if (result.empty()) {
                co_return UploadedChunkCoverage{ 0, -1 };
            }

            UploadedChunkCoverage coverage;
            coverage.uploaded_count = result[0]["uploaded_count"].as<uint64_t>();
            coverage.max_chunk_index = result[0]["max_chunk_index"].as<int64_t>();
            co_return coverage;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to get uploaded chunk coverage: " << e.base().what();
            co_return std::nullopt;
        }
    }

    auto UploadService::InvalidateFileListCache(uint64_t user_id, const std::vector<uint64_t>& folder_ids)
        -> drogon::Task<void> {
        std::vector<std::string> keys_to_delete;
        for (const auto folder_id : folder_ids) {
            for (const auto& type : {"all", "file", "folder"}) {
                for (const auto& sort : {"name", "size", "created_at", "updated_at"}) {
                    for (const auto& order : {"asc", "desc"}) {
                        for (int page = 1; page <= 3; ++page) {
                            keys_to_delete.push_back(
                                disk::redis::RedisKeyPrefix::BuildFileListCacheKey(
                                    user_id, folder_id, type, sort, order, page
                                )
                            );
                        }
                    }
                }
            }
        }
        if (!keys_to_delete.empty()) {
            co_await m_redis_service->MDelete(keys_to_delete);
        }
    }

} ///< namespace disk::file
