/**
 * @file FileService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileService.hpp"

#include <chrono>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <json/writer.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/UploadTaskChunks.hpp"
#include "storage/IFileStorage.hpp"
#include "utils/BatchUtils.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"

namespace disk::file {

    using disk::utils::BatchUtils;
    using disk::utils::ConfigMgr;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using disk::utils::FileHashUtil;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::Trash;
    using drogon_model::disk::UploadTaskChunks;
    using drogon_model::disk::UploadTasks;

    namespace {
        auto EscapeSqlLiteral(const std::string& input) -> std::string {
            std::string escaped;
            escaped.reserve(input.size());
            for (const auto ch : input) {
                if (ch == '\'') {
                    escaped += "''";
                } else {
                    escaped.push_back(ch);
                }
            }
            return escaped;
        }
    } // namespace

    // ==================== 构造函数 ====================

    FileService::FileService(drogon::orm::DbClientPtr db_client, storage::IFileStorage* storage)
        : m_db_client(std::move(db_client)), m_storage(storage) {
        StartUploadTaskCacheMaintenance();
        LOG_DEBUG << "FileService initialization completed";
    }

    // ==================== InitUpload ====================

    auto FileService::InitUpload(InitUploadRequest request, uint64_t user_id)
        -> drogon::Task<Result<InitUploadResponse>> {

        LOG_DEBUG << "Starting initialize upload: filename=\"" << request.filename
                  << "\", file_size=" << request.file_size << ", file_hash=" << request.file_hash
                  << ", parent_id=" << request.parent_id << ", user_id=" << user_id;

        // 1. 检测秒传：查找已存在的内容
        auto existing_content = co_await FindExistingContent(request.file_hash);
        if (existing_content.has_value()) {
            LOG_INFO << "Instant upload check successful: file_hash=" << request.file_hash
                     << ", content_id=" << existing_content.value();

            // 检查同名文件是否存在
            if (co_await IsFilenameExists(request.parent_id, request.filename, user_id)) {
                LOG_WARN << "File with same name already exists: " << request.filename;
                co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
            }

            // 创建 Files 记录
            try {
                auto increment_result = co_await m_db_client->execSqlCoro(
                    "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = ?",
                    existing_content.value()
                );
                if (increment_result.affectedRows() == 0) {
                    LOG_WARN << "File content not found for instant upload: content_id="
                             << existing_content.value();
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to update file content")
                    );
                }

                CoroMapper<FileContents> content_mapper(m_db_client);
                auto content = co_await content_mapper.findOne(
                    Criteria(FileContents::Cols::_id, CompareOperator::EQ, existing_content.value())
                );

                // 创建文件记录
                Files file;
                file.setUserId(user_id);
                file.setContentId(existing_content.value());
                file.setFolderId(request.parent_id);
                file.setName(request.filename);
                file.setExtension(ExtractExtension(request.filename));
                file.setSize(request.file_size);
                file.setMimeType(content.getValueOfMimeType());
                file.setPath(""); // 路径由前端拼接
                file.setIsFavorite(0);
                file.setDownloadCount(0);

                CoroMapper<Files> file_mapper(m_db_client);
                file = co_await file_mapper.insert(file);

                // 更新用户存储使用量（秒传不增加实际存储）
                // 注：秒传时 storage_used 不增加，因为物理文件已存在

                // 构造响应
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

                LOG_INFO << "Instant upload completed: file_id=" << file.getValueOfId();
                co_return response;

            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_ERROR << "Instant upload create file record failed: " << e.base().what();
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create file record")
                );
            }
        }

        // 2. 检测断点续传
        auto existing_task = co_await FindExistingTask(user_id, request.file_hash);
        if (existing_task.has_value()) {
            const auto& task = existing_task.value();
            LOG_INFO << "Resume upload check successful: upload_id=" << task.getValueOfId();
            InvalidateUploadTaskCache(task.getValueOfId());

            // 从 upload_task_chunks 表查询已上传分片
            auto chunk_result = co_await m_db_client->execSqlCoro(
                "SELECT chunk_index FROM upload_task_chunks WHERE task_id = ? ORDER BY chunk_index",
                task.getValueOfId()
            );

            InitUploadResponse response;
            response.upload_id = task.getValueOfId();
            response.chunk_size = task.getValueOfChunkSize();
            response.total_chunks = task.getValueOfTotalChunks();
            response.instant_upload = false;

            response.uploaded_chunks.clear();
            for (const auto& row : chunk_result) {
                response.uploaded_chunks.push_back(row["chunk_index"].as<uint32_t>());
            }

            co_return response;
        }

        // 3. 预留存储配额
        auto quota_result = co_await ReserveStorageQuota(user_id, request.file_size);
        if (!quota_result) {
            LOG_WARN << "Storage quota reservation failed: user_id=" << user_id;
            co_return std::unexpected(quota_result.error());
        }

        // 4. 创建新的上传任务
        auto config = ConfigMgr::GetInstance();
        auto chunk_size = config->GetChunkSize();
        auto total_chunks = static_cast<uint32_t>(
            std::ceil(static_cast<double>(request.file_size) / static_cast<double>(chunk_size))
        );
        auto expiry_seconds = config->GetUploadTaskExpirySeconds();

        // 生成上传 ID
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
        task.setStatus(0); // 进行中
        task.setExpiresAt(trantor::Date::now().after(expiry_seconds));

        bool create_task_failed = false;
        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            task = co_await mapper.insert(task);

            LOG_INFO << "Upload task created successfully: upload_id=" << task.getValueOfId()
                     << ", total_chunks=" << total_chunks;
            InvalidateUploadTaskCache(task.getValueOfId());

            // 预创建临时上传目录，避免每个分片写入时重复创建
            auto ensure_result = co_await m_storage->EnsureUploadTempDir(task.getValueOfId());
            if (!ensure_result) {
                LOG_WARN << "Failed to ensure upload temp directory: upload_id="
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
            LOG_ERROR << "Failed to create upload task: " << e.base().what();
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

    // ==================== UploadChunk ====================

    auto FileService::UploadChunk(
        std::string upload_id,
        uint32_t chunk_index,
        std::string chunk_hash,
        std::string_view chunk_data,
        uint64_t user_id
    ) -> drogon::Task<Result<UploadChunkResponse>> {

        auto start = std::chrono::steady_clock::now();

        LOG_DEBUG << "Starting upload chunk: upload_id=" << upload_id
                  << ", chunk_index=" << chunk_index << ", chunk_hash=" << chunk_hash
                  << ", data_size=" << chunk_data.size();

        // 1. 优先读取短 TTL 上传任务缓存，命中后避免重复查询数据库
        auto cached_task = TryGetUploadTaskCacheEntry(upload_id, user_id);
        if (!cached_task.has_value()) {
            auto task_result = co_await FindUploadTask(upload_id, user_id);
            if (!task_result) {
                LOG_WARN << "Upload task verification failed: " << upload_id;

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                LOG_INFO << "[upload_chunk] duration_us=" << duration_us
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

        // 2. 验证任务未过期
        if (task.expires_at < trantor::Date::now()) {
            LOG_WARN << "Upload task expired: " << upload_id;
            InvalidateUploadTaskCache(upload_id);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::UploadTaskNotFound, "Upload task expired")
            );
        }

        // 3. 验证分片索引有效
        if (chunk_index >= task.total_chunks) {
            LOG_WARN << "Chunk index out of range: chunk_index=" << chunk_index
                     << ", total_chunks=" << task.total_chunks;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Chunk index out of range")
            );
        }

        // 4. 验证分片哈希
        auto actual_hash = FileHashUtil::HashMd5(std::string{ chunk_data });
        if (actual_hash != chunk_hash) {
            LOG_WARN << "Chunk hash mismatch: expected=" << chunk_hash
                     << ", actual=" << actual_hash;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Chunk hash mismatch")
            );
        }

        // 5. 创建临时目录并写入分片
        auto write_result = co_await m_storage->WriteChunk(upload_id, chunk_index, chunk_data);
        if (!write_result) {
            LOG_ERROR << "Failed to write chunk file: upload_id=" << upload_id
                      << ", chunk_index=" << chunk_index << ", error="
                      << static_cast<int>(write_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(write_result.error());
        }

        // 6. 记录已上传分片（幂等：INSERT IGNORE 允许重复上传同一分片）
        try {
            co_await m_db_client->execSqlCoro(
                "INSERT IGNORE INTO upload_task_chunks (task_id, chunk_index, uploaded_at) VALUES (?, ?, NOW())",
                upload_id,
                chunk_index
            );

            LOG_DEBUG << "Chunk upload successful: upload_id=" << upload_id
                      << ", chunk_index=" << chunk_index;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=success upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            UploadChunkResponse response;
            response.chunk_index = chunk_index;
            response.uploaded = true;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to record chunk upload: " << e.base().what();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to record chunk upload")
            );
        }
    }

    // ==================== CompleteUpload ====================

    auto FileService::CompleteUpload(std::string upload_id, uint64_t user_id)
        -> drogon::Task<Result<CompleteUploadResponse>> {

        auto start = std::chrono::steady_clock::now();

        LOG_DEBUG << "Starting complete upload: upload_id=" << upload_id << ", user_id=" << user_id;

        // 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            LOG_WARN << "Upload task verification failed: " << upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id;

            co_return std::unexpected(task_result.error());
        }

        auto task = task_result.value();

        // 2. Check idempotency: already completed
        if (task.getValueOfStatus() == 1) {
            LOG_INFO << "Upload task already completed: upload_id=" << upload_id;
            InvalidateUploadTaskCache(upload_id);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[complete_upload] duration_us=" << duration_us
                     << " outcome=success upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return CompleteUploadResponse{};
        }

        // 3. 用一次有序分片索引查询同时验证数量与连续性
        auto uploaded_chunks_result = co_await m_db_client->execSqlCoro(
            "SELECT chunk_index FROM upload_task_chunks WHERE task_id = ? ORDER BY chunk_index",
            upload_id
        );
        auto uploaded_count = uploaded_chunks_result.size();

        if (uploaded_count != task.getValueOfTotalChunks()) {
            LOG_WARN << "Not all chunks uploaded: uploaded=" << uploaded_count
                     << ", total=" << task.getValueOfTotalChunks();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
            );
        }

        for (uint32_t expected_index = 0; expected_index < task.getValueOfTotalChunks();
             ++expected_index) {
            auto actual_index = uploaded_chunks_result[expected_index]["chunk_index"].as<uint32_t>();
            if (actual_index != expected_index) {
                LOG_WARN << "Uploaded chunks are not contiguous: upload_id=" << upload_id
                         << ", expected_index=" << expected_index
                         << ", actual_index=" << actual_index;

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                LOG_INFO << "[complete_upload] duration_us=" << duration_us
                         << " outcome=failure upload_id=" << upload_id
                         << " total_chunks=" << task.getValueOfTotalChunks();

                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
                );
            }
        }

        // 3. 组装分片（同时计算 MD5 + SHA256）
        auto assemble_result = co_await m_storage->AssembleChunks(upload_id, task.getValueOfTotalChunks());
        if (!assemble_result) {
            LOG_ERROR << "Failed to assemble chunks: upload_id=" << upload_id
                      << ", error=" << static_cast<int>(assemble_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(assemble_result.error());
        }
        const auto& assembled = assemble_result.value();
        const auto& assemble_path = assembled.path;

        // 4. 使用组装时计算的哈希值
        const auto& final_hash = assembled.md5_hash;
        const auto& precomputed_sha256 = assembled.sha256_hash;
        if (final_hash != task.getValueOfFileHash()) {
            LOG_ERROR << "File hash mismatch: expected=" << task.getValueOfFileHash()
                      << ", actual=" << final_hash;
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                LOG_WARN << "Failed to cleanup assemble file after hash mismatch: "
                         << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "File hash verification failed")
            );
        }

        LOG_DEBUG << "File hash verification passed: " << final_hash;

        if (co_await IsFilenameExists(task.getValueOfFolderId(), task.getValueOfFilename(), user_id)) {
            LOG_WARN << "File with same name already exists: " << task.getValueOfFilename();
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                LOG_WARN << "Failed to cleanup assemble file on duplicate name: "
                         << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
        }

        auto existing_content = co_await FindExistingContent(final_hash);
        std::filesystem::path final_storage_path;
        std::string final_sha256;
        bool should_compensate_storage_file = false;

        if (existing_content.has_value()) {
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                LOG_WARN << "Failed to cleanup assemble file after dedup: "
                         << static_cast<int>(delete_result.error().code);
            }
            LOG_DEBUG << "File dedup successful: content_id=" << existing_content.value();
        } else {
            auto promote_result = co_await m_storage->PromoteToFinal(assemble_path, final_hash);
            if (!promote_result) {
                LOG_ERROR << "Failed to move file to final storage: error="
                          << static_cast<int>(promote_result.error().code);
                auto cleanup_result = co_await m_storage->DeletePath(assemble_path);
                if (!cleanup_result) {
                    LOG_WARN << "Failed to cleanup assemble file after promote failure: "
                             << static_cast<int>(cleanup_result.error().code);
                }

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                LOG_INFO << "[complete_upload] duration_us=" << duration_us
                         << " outcome=failure upload_id=" << upload_id
                         << " total_chunks=" << task.getValueOfTotalChunks();

                co_return std::unexpected(promote_result.error());
            }

            final_storage_path = promote_result.value();
            final_sha256 = precomputed_sha256;
            should_compensate_storage_file = true;
        }

        std::shared_ptr<drogon::orm::Transaction> transaction;
        Files file;
        bool db_operation_failed = false;
        try {
            transaction = co_await m_db_client->newTransactionCoro();

            CoroMapper<FileContents> content_mapper(transaction);
            CoroMapper<Files> file_mapper(transaction);

            uint64_t content_id = 0;
            if (existing_content.has_value()) {
                content_id = existing_content.value();
                auto increment_result = co_await transaction->execSqlCoro(
                    "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = ?",
                    content_id
                );
                if (increment_result.affectedRows() == 0) {
                    LOG_WARN << "File content not found when finalizing upload: content_id="
                             << content_id;
                    throw std::runtime_error("Failed to increment file content reference count");
                }
            } else {
                FileContents content;
                content.setHashMd5(final_hash);
                content.setHashSha256(final_sha256);
                content.setSize(task.getValueOfFileSize());
                content.setStoragePath(final_storage_path.string());
                content.setMimeType("");
                content.setRefCount(1);

                content = co_await content_mapper.insert(content);
                content_id = content.getValueOfId();
                LOG_DEBUG << "FileContents created successfully: content_id=" << content_id;
            }

            file.setUserId(user_id);
            file.setContentId(content_id);
            file.setFolderId(task.getValueOfFolderId());
            file.setName(task.getValueOfFilename());
            file.setExtension(ExtractExtension(task.getValueOfFilename()));
            file.setSize(task.getValueOfFileSize());
            file.setMimeType("");
            file.setPath("");
            file.setIsFavorite(0);
            file.setDownloadCount(0);

            file = co_await file_mapper.insert(file);

            auto transfer_result = co_await transaction->execSqlCoro(
                "UPDATE users SET storage_reserved = GREATEST(storage_reserved - ?, 0), " "storage_used = storage_used + ? WHERE id = ?",
                task.getValueOfFileSize(),
                task.getValueOfFileSize(),
                user_id
            );

            if (transfer_result.affectedRows() == 0) {
                throw std::runtime_error("Failed to transfer reserved quota to used");
            }

            auto finalize_result = co_await transaction->execSqlCoro(
                "UPDATE upload_tasks SET status = 1, finalized_at = NOW() WHERE id = ? AND status = 0",
                upload_id
            );
            if (finalize_result.affectedRows() == 0) {
                throw std::runtime_error("Failed to finalize upload task");
            }

            co_await transaction->execSqlCoro(
                "DELETE FROM upload_task_chunks WHERE task_id = ?",
                upload_id
            );

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Database operation failed: " << e.base().what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    LOG_ERROR << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            db_operation_failed = true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Database operation failed: " << e.what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    LOG_ERROR << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            db_operation_failed = true;
        }

        if (db_operation_failed) {
            if (should_compensate_storage_file) {
                auto cleanup_result = co_await m_storage->DeletePath(final_storage_path);
                if (!cleanup_result) {
                    LOG_ERROR << "Compensation failed, orphan storage file may remain: "
                              << final_storage_path;
                }
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Database operation failed")
            );
        }

        LOG_INFO << "Files record created successfully: file_id=" << file.getValueOfId();
        InvalidateUploadTaskCache(upload_id);

        // 7. Cleanup temp directory
        auto cleanup_result = co_await m_storage->CleanupTemp(upload_id);
        if (!cleanup_result) {
            LOG_WARN << "Failed to cleanup temp artifacts: "
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

        LOG_INFO << "File upload completed: file_id=" << file.getValueOfId()
                 << ", filename=" << task.getValueOfFilename();

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        LOG_INFO << "[complete_upload] duration_us=" << duration_us
                 << " outcome=success upload_id=" << upload_id
                 << " total_chunks=" << task.getValueOfTotalChunks();

        co_return response;
    }

    auto FileService::CancelUpload(std::string upload_id, uint64_t user_id)
        -> drogon::Task<Result<void>> {

        LOG_DEBUG << "Starting cancel upload: upload_id=" << upload_id << ", user_id=" << user_id;

        // 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            LOG_WARN << "Upload task verification failed: " << upload_id;
            co_return std::unexpected(task_result.error());
        }
        const auto task = task_result.value();

        // 2. Check idempotency: already in terminal state
        if (task.getValueOfStatus() != 0) {
            LOG_INFO << "Upload task already in terminal state: upload_id=" << upload_id
                     << ", status=" << task.getValueOfStatus();
            co_return {};
        }

        // 3. Release reserved quota
        co_await ReleaseReservedQuota(user_id, task.getValueOfReservedBytes());

        // 4. Set terminal state (status=2 cancelled)
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE upload_tasks SET status = 2, finalized_at = NOW(), " "fail_reason = '用户取消' WHERE id = ? AND status = 0",
                upload_id
            );
            InvalidateUploadTaskCache(upload_id);
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to set cancel terminal state: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to cancel upload task")
            );
        }

        // 5. Cleanup chunk tracking rows
        try {
            co_await m_db_client->execSqlCoro(
                "DELETE FROM upload_task_chunks WHERE task_id = ?",
                upload_id
            );
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Failed to cleanup upload_task_chunks: " << e.base().what();
        }

        // 6. Cleanup temp directory
        auto cleanup_result = co_await m_storage->CleanupTemp(upload_id);
        if (!cleanup_result) {
            LOG_WARN << "Failed to delete temp directory: upload_id=" << upload_id
                     << ", error=" << static_cast<int>(cleanup_result.error().code);
        }

        LOG_INFO << "Upload task cancelled: upload_id=" << upload_id;
        co_return {};
    }

    // ==================== GetFileList ====================

    auto FileService::GetFileList(FileListRequest request, uint64_t user_id)
        -> drogon::Task<Result<FileListResponse>> {

        LOG_DEBUG << "Starting get file list: parent_id=" << request.parent_id
                  << ", page=" << request.page << ", page_size=" << request.page_size
                  << ", sort_by=" << request.sort_by << ", sort_order=" << request.sort_order
                  << ", type=" << request.type << ", user_id=" << user_id;

        // 1. 验证 parent_id 文件夹存在且属于用户（如果 parent_id != 0）
        if (request.parent_id != 0) {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                auto folder = co_await folder_mapper.findOne(
                    Criteria(Folders::Cols::_id, CompareOperator::EQ, request.parent_id) &&
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id)
                );
                LOG_DEBUG << "Folder verification passed: folder_id=" << request.parent_id;
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "Folder not found or no permission: folder_id=" << request.parent_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        // 2. 使用 SQL 查询（JOIN 消除 N+1， LIMIT/OFFSET 宻除内存分页）
        std::vector<FileListItem> items;
        int total = 0;
        int total_pages = 0;

        // 构建 ORDER BY 子句
        std::string order_by = "name";
        if (request.sort_by == "size") {
            order_by = "size";
        } else if (request.sort_by == "created_at") {
            order_by = "created_at";
        } else if (request.sort_by == "updated_at") {
            order_by = "updated_at";
        }

        std::string order_dir = (request.sort_order == "desc") ? "DESC" : "ASC";

        auto offset = (request.page - 1) * request.page_size;

        try {
            if (request.type == "all") {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM (" "  SELECT f.id FROM files f WHERE f.folder_id = ? AND f.user_id = ? " "  UNION ALL " "  SELECT fo.id FROM folders fo WHERE fo.parent_id = ? AND fo.user_id = ? " ") AS combined",
                    request.parent_id,
                    user_id,
                    request.parent_id,
                    user_id
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                }

                std::string data_sql =
                    "SELECT * FROM (" "  SELECT f.id, f.name, 'file' AS type, f.size, f.mime_type, " "         COALESCE(fc.hash_md5, '') AS hash, 0 AS item_count, f.created_at, f.updated_at " "  FROM files f " "  LEFT JOIN file_contents fc ON f.content_id = fc.id " "  WHERE f.folder_id = ? AND f.user_id = ? " "  UNION ALL " "  SELECT fo.id, fo.name, 'folder' AS type, 0 AS size, '' AS mime_type, " "         '' AS hash, fo.item_count, fo.created_at, fo.updated_at " "  FROM folders fo " "  WHERE fo.parent_id = ? AND fo.user_id = ? " ") AS combined " "ORDER BY " + order_by + " " + order_dir + " " "LIMIT ? OFFSET ?";

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = row["type"].as<std::string>();
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "file") {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM files WHERE folder_id = ? AND user_id = ?",
                    request.parent_id,
                    user_id
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                }

                std::string data_sql =
                    "SELECT f.id, f.name, f.size, f.mime_type, " "       COALESCE(fc.hash_md5, '') AS hash, f.created_at, f.updated_at " "FROM files f " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE f.folder_id = ? AND f.user_id = ? " "ORDER BY " + order_by + " " + order_dir + " " "LIMIT ? OFFSET ?";

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "file";
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.item_count = 0;
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "folder") {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM folders WHERE parent_id = ? AND user_id = ?",
                    request.parent_id,
                    user_id
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                }

                std::string data_sql =
                    "SELECT id, name, item_count, created_at, updated_at " "FROM folders " "WHERE parent_id = ? AND user_id = ? " "ORDER BY " + order_by + " " + order_dir + " " "LIMIT ? OFFSET ?";

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "folder";
                    item.size = 0;
                    item.mime_type = "";
                    item.hash = "";
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }
            }

            total_pages = request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Failed to query file list: " << e.base().what();
        }

        // 3. 构造响应
        FileListResponse response;
        response.items = items;
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };

        LOG_DEBUG << "File list retrieved successfully: total=" << total
                  << ", page=" << request.page;
        co_return response;
    }

    // ==================== GetDownloadInfo ====================

    auto FileService::GetDownloadInfo(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfoResponse>> {

        LOG_DEBUG << "Starting get download info: file_id=" << file_id << ", user_id=" << user_id;

        // 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            // 2. 获取文件内容信息
            if (!file.getContentId()) {
                LOG_ERROR << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            // 3. 构造响应
            DownloadInfoResponse response;
            response.file_id = file.getValueOfId();
            response.filename = file.getValueOfName();
            response.file_size = file.getValueOfSize();
            response.file_hash = content.getValueOfHashMd5();
            response.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                     file.getValueOfMimeType();
            response.supports_range = true;

            LOG_DEBUG << "Download info retrieved successfully: filename=" << response.filename
                      << ", size=" << response.file_size;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    // ==================== GetDownloadData ====================

    auto FileService::GetDownloadData(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfo>> {

        LOG_DEBUG << "Starting get download data: file_id=" << file_id << ", user_id=" << user_id;

        // 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            // 2. 获取文件内容信息
            if (!file.getContentId()) {
                LOG_ERROR << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            // 3. 构造响应
            DownloadInfo info;
            info.file_id = file.getValueOfId();
            info.filename = file.getValueOfName();
            info.file_size = file.getValueOfSize();
            info.file_hash = content.getValueOfHashMd5();
            info.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                 file.getValueOfMimeType();
            info.storage_path = content.getValueOfStoragePath();
            info.supports_range = true;

            LOG_DEBUG << "Download data retrieved successfully: filename=" << info.filename
                      << ", storage_path=" << info.storage_path;
            co_return info;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    // ==================== Rename ====================

    auto FileService::Rename(uint64_t file_id, std::string new_name, uint64_t user_id)
        -> drogon::Task<Result<RenameResponse>> {

        LOG_DEBUG << "Starting rename file: file_id=" << file_id << ", new_name=\"" << new_name
                  << "\""
                  << ", user_id=" << user_id;

        try {
            CoroMapper<Files> mapper(m_db_client);
            auto file = co_await mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            if (file.getValueOfName() == new_name) {
                LOG_DEBUG << "New name same as current name, skipping update";
                RenameResponse response;
                response.id = file.getValueOfId();
                response.name = file.getValueOfName();
                response.updated_at = file.getValueOfUpdatedAt().toDbStringLocal();
                co_return response;
            }

            auto folder_id = file.getValueOfFolderId();
            if (co_await IsFilenameExists(folder_id, new_name, user_id)) {
                LOG_WARN << "Target folder already has file with same name: " << new_name;
                co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
            }

            file.setName(new_name);
            file.setExtension(ExtractExtension(new_name));
            file.setUpdatedAt(trantor::Date::now());
            co_await mapper.update(file);

            LOG_INFO << "File rename successful: file_id=" << file_id << ", new_name=\"" << new_name
                     << "\"";

            RenameResponse response;
            response.id = file.getValueOfId();
            response.name = new_name;
            response.updated_at = file.getValueOfUpdatedAt().toDbStringLocal();
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    // ==================== Move ====================

    auto FileService::Move(MoveRequest request, uint64_t user_id)
        -> drogon::Task<Result<MoveResponse>> {

        LOG_DEBUG << "Starting move file: file_ids.size()=" << request.file_ids.size()
                  << ", target_folder_id=" << request.target_folder_id << ", user_id=" << user_id;

        if (request.target_folder_id != 0) {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                co_await folder_mapper.findOne(
                    Criteria(Folders::Cols::_id, CompareOperator::EQ, request.target_folder_id) &&
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id)
                );
                LOG_DEBUG << "Target folder verification passed: folder_id="
                          << request.target_folder_id;
            } catch (const drogon::orm::DrogonDbException&) {
                LOG_WARN << "Target folder not found or no permission: folder_id="
                         << request.target_folder_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        int moved_count = 0;
        std::unordered_set<uint64_t> already_moved_ids;

        auto chunks = BatchUtils::Chunk(request.file_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }

            auto in_clause = BatchUtils::BuildNumericInClause(chunk);

            std::unordered_map<uint64_t, Files> file_map;
            file_map.reserve(chunk.size());

            try {
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, " "       is_favorite, download_count, created_at, updated_at " "FROM files WHERE id IN (" + in_clause + ") AND user_id = ?",
                    user_id
                );

                for (const auto& row : result) {
                    auto file = Files(row);
                    file_map[file.getValueOfId()] = std::move(file);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "File batch fetch failed in move, skipping chunk: " << e.base().what();
                for (const auto& file_id : chunk) {
                    LOG_WARN << "File not found or move failed, skipping: file_id=" << file_id;
                }
                continue;
            }

            // Extract candidate names from already-fetched file_map
            std::vector<std::string> candidate_names;
            candidate_names.reserve(file_map.size());
            for (const auto& [id, file] : file_map) {
                candidate_names.push_back(file.getValueOfName());
            }

            std::unordered_set<std::string> occupied_names;
            if (!candidate_names.empty()) {
                try {
                    // Build safe string IN clause (escape single quotes)
                    std::ostringstream name_in;
                    for (size_t i = 0; i < candidate_names.size(); ++i) {
                        if (i > 0) {
                            name_in << ",";
                        }
                        std::string escaped = candidate_names[i];
                        size_t pos = 0;
                        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
                            escaped.replace(pos, 1, "''");
                            pos += 2;
                        }
                        name_in << "'" << escaped << "'";
                    }

                    auto conflict_result = co_await m_db_client->execSqlCoro(
                        "SELECT name FROM files WHERE folder_id = ? AND user_id = ? AND name IN (" + name_in.str() + ")",
                        request.target_folder_id,
                        user_id
                    );

                    for (const auto& row : conflict_result) {
                        occupied_names.insert(row["name"].as<std::string>());
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "Filename conflict query failed in move, skipping chunk: "
                             << e.base().what();
                    continue;
                }
            }

            std::vector<uint64_t> valid_ids;
            valid_ids.reserve(chunk.size());

            for (const auto& file_id : chunk) {
                if (already_moved_ids.contains(file_id)) {
                    LOG_DEBUG << "File already in target folder, skipping: file_id=" << file_id;
                    ++moved_count;
                    continue;
                }

                auto it = file_map.find(file_id);
                if (it == file_map.end()) {
                    LOG_WARN << "File not found or move failed, skipping: file_id=" << file_id;
                    continue;
                }

                auto& file = it->second;

                if (file.getValueOfFolderId() == request.target_folder_id) {
                    LOG_DEBUG << "File already in target folder, skipping: file_id=" << file_id;
                    ++moved_count;
                    occupied_names.insert(file.getValueOfName());
                    already_moved_ids.insert(file_id);
                    continue;
                }

                if (occupied_names.contains(file.getValueOfName())) {
                    LOG_WARN << "Target folder already has file with same name, skipping: "
                             << file.getValueOfName();
                    continue;
                }

                valid_ids.push_back(file_id);
                occupied_names.insert(file.getValueOfName());
                already_moved_ids.insert(file_id);

                ++moved_count;
                LOG_DEBUG << "File move successful: file_id=" << file_id;
            }

            if (!valid_ids.empty()) {
                auto update_in_clause = BatchUtils::BuildNumericInClause(valid_ids);
                try {
                    co_await m_db_client->execSqlCoro(
                        "UPDATE files SET folder_id = ?, updated_at = ? WHERE id IN (" +
                            update_in_clause + ")",
                        request.target_folder_id,
                        trantor::Date::now()
                    );
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "Batch move update failed: " << e.base().what();
                    moved_count -= static_cast<int>(valid_ids.size());
                }
            }
        }

        LOG_INFO << "File move completed: moved_count=" << moved_count;

        MoveResponse response;
        response.moved_count = moved_count;
        co_return response;
    }

    // ==================== Copy ====================

    auto FileService::Copy(CopyRequest request, uint64_t user_id)
        -> drogon::Task<Result<CopyResponse>> {

        LOG_DEBUG << "Starting copy file: file_ids.size()=" << request.file_ids.size()
                  << ", target_folder_id=" << request.target_folder_id << ", user_id=" << user_id;

        if (request.target_folder_id != 0) {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                co_await folder_mapper.findOne(
                    Criteria(Folders::Cols::_id, CompareOperator::EQ, request.target_folder_id) &&
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id)
                );
                LOG_DEBUG << "Target folder verification passed: folder_id="
                          << request.target_folder_id;
            } catch (const drogon::orm::DrogonDbException&) {
                LOG_WARN << "Target folder not found or no permission: folder_id="
                         << request.target_folder_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        uint64_t total_copy_size = 0;
        std::vector<std::pair<uint64_t, Files>> files_to_copy;

        {
            auto id_chunks = BatchUtils::Chunk(request.file_ids, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : id_chunks) {
                if (chunk.empty()) {
                    continue;
                }

                auto in_clause = BatchUtils::BuildNumericInClause(chunk);

                std::unordered_map<uint64_t, Files> file_map;
                file_map.reserve(chunk.size());

                try {
                    auto result = co_await m_db_client->execSqlCoro(
                        "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, " "       is_favorite, download_count, created_at, updated_at " "FROM files WHERE id IN (" + in_clause + ") AND user_id = ?",
                        user_id
                    );

                    for (const auto& row : result) {
                        auto file = Files(row);
                        file_map[file.getValueOfId()] = std::move(file);
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "File batch fetch failed in copy, skipping chunk: " << e.base().what();
                    for (const auto& file_id : chunk) {
                        LOG_WARN << "File not found or no permission, skipping: file_id=" << file_id;
                    }
                    continue;
                }

                for (const auto& file_id : chunk) {
                    auto it = file_map.find(file_id);
                    if (it == file_map.end()) {
                        LOG_WARN << "File not found or no permission, skipping: file_id=" << file_id;
                        continue;
                    }

                    total_copy_size += it->second.getValueOfSize();
                    files_to_copy.emplace_back(file_id, it->second);
                }
            }
        }

        if (total_copy_size == 0) {
            LOG_INFO << "No files can be copied after validation";
            CopyResponse response;
            response.copied_count = 0;
            response.new_files = {};
            co_return response;
        }

        auto quota_result = co_await CheckStorageQuota(m_db_client, user_id, total_copy_size);
        if (!quota_result) {
            LOG_WARN << "Storage quota check failed for copy: user_id=" << user_id
                     << ", total_copy_size=" << total_copy_size;
            co_return std::unexpected(quota_result.error());
        }

        int copied_count = 0;
        uint64_t actual_copy_size = 0;
        std::vector<FileIdMapping> new_files;

        auto copy_chunks = BatchUtils::Chunk(files_to_copy, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : copy_chunks) {
            if (chunk.empty()) {
                continue;
            }

            // ── 读取阶段（事务外）：名称冲突检查 ──
            std::vector<std::string> candidate_names;
            candidate_names.reserve(chunk.size());
            for (const auto& [old_id, file] : chunk) {
                candidate_names.push_back(file.getValueOfName());
            }

            std::unordered_set<std::string> occupied_names;
            if (!candidate_names.empty()) {
                try {
                    std::ostringstream name_in;
                    for (size_t i = 0; i < candidate_names.size(); ++i) {
                        if (i > 0) {
                            name_in << ",";
                        }
                        std::string escaped = candidate_names[i];
                        size_t pos = 0;
                        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
                            escaped.replace(pos, 1, "''");
                            pos += 2;
                        }
                        name_in << "'" << escaped << "'";
                    }

                    auto conflict_result = co_await m_db_client->execSqlCoro(
                        "SELECT name FROM files WHERE folder_id = ? AND user_id = ? AND name IN (" + name_in.str() + ")",
                        request.target_folder_id,
                        user_id
                    );

                    for (const auto& row : conflict_result) {
                        occupied_names.insert(row["name"].as<std::string>());
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "Filename conflict query failed in copy, skipping chunk: "
                             << e.base().what();
                    continue;
                }
            }

            struct PendingCopyItem {
                uint64_t old_id;
                Files file;
            };

            std::vector<PendingCopyItem> pending_items;
            pending_items.reserve(chunk.size());
            std::unordered_map<uint64_t, uint64_t> content_ref_increment;

            for (const auto& [old_id, file] : chunk) {
                if (occupied_names.contains(file.getValueOfName())) {
                    LOG_WARN << "Target folder already has file with same name, skipping: "
                             << file.getValueOfName();
                    continue;
                }

                occupied_names.insert(file.getValueOfName());

                if (file.getContentId()) {
                    content_ref_increment[*file.getContentId()] += 1;
                }

                pending_items.push_back({ .old_id = old_id, .file = file });
            }

            // ── 读取阶段（事务外）：内容存在性校验 ──
            std::unordered_set<uint64_t> existing_content_ids;
            if (!content_ref_increment.empty()) {
                std::vector<uint64_t> content_ids;
                content_ids.reserve(content_ref_increment.size());
                for (const auto& [content_id, _] : content_ref_increment) {
                    content_ids.push_back(content_id);
                }

                auto content_in_clause = BatchUtils::BuildNumericInClause(content_ids);
                existing_content_ids.reserve(content_ids.size());

                try {
                    auto content_result = co_await m_db_client->execSqlCoro(
                        "SELECT id FROM file_contents WHERE id IN (" + content_in_clause + ")"
                    );
                    for (const auto& row : content_result) {
                        existing_content_ids.insert(row["id"].as<uint64_t>());
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "File content batch query failed in copy, skipping chunk: "
                             << e.base().what();
                    continue;
                }
            }

            std::unordered_set<uint64_t> missing_content_ids;
            for (const auto& [content_id, _] : content_ref_increment) {
                if (!existing_content_ids.contains(content_id)) {
                    missing_content_ids.insert(content_id);
                }
            }

            std::vector<std::pair<uint64_t, const Files*>> valid_items;
            valid_items.reserve(pending_items.size());

            for (const auto& pending : pending_items) {
                auto content_id_ptr = pending.file.getContentId();
                if (content_id_ptr && missing_content_ids.contains(*content_id_ptr)) {
                    LOG_WARN << "File content not found during copy: content_id=" << *content_id_ptr;
                    continue;
                }
                valid_items.emplace_back(pending.old_id, &pending.file);
            }

            // ── 事务阶段：ref_count 递增 + 文件行插入，原子提交/回滚 ──
            if (!valid_items.empty()) {
                std::unordered_map<uint64_t, uint64_t> old_id_to_size;
                for (const auto& [old_id, file_ptr] : valid_items) {
                    old_id_to_size[old_id] = file_ptr->getValueOfSize();
                }

                std::shared_ptr<drogon::orm::Transaction> txn;
                bool batch_failed = false;
                try {
                    txn = co_await m_db_client->newTransactionCoro();

                    auto incremented_ids = co_await IncrementContentRefCount(
                        txn,
                        content_ref_increment,
                        existing_content_ids
                    );

                    // 重新校验：仅保留实际成功递增 ref_count 的条目
                    std::vector<std::pair<uint64_t, const Files*>> txn_valid_items;
                    txn_valid_items.reserve(valid_items.size());
                    for (const auto& [old_id, file_ptr] : valid_items) {
                        auto cid = file_ptr->getContentId();
                        if (cid && !incremented_ids.contains(*cid)) {
                            LOG_WARN << "Content ref_count increment skipped in txn, dropping file: content_id="
                                     << *cid;
                            continue;
                        }
                        txn_valid_items.emplace_back(old_id, file_ptr);
                    }

                    auto id_mappings = co_await InsertCopiedFiles(
                        txn,
                        user_id,
                        request.target_folder_id,
                        txn_valid_items
                    );

                    // 事务自动提交（协程正常完成）

                    for (const auto& [old_id, new_id] : id_mappings) {
                        ++copied_count;
                        actual_copy_size += old_id_to_size[old_id];
                        new_files.push_back({ .old_id = old_id, .new_id = new_id });
                        LOG_DEBUG << "File copy successful: old_id=" << old_id
                                  << ", new_id=" << new_id;
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_ERROR << "Copy batch transaction failed (DB): " << e.base().what();
                    if (txn) {
                        try {
                            txn->rollback();
                        } catch (const std::exception& rb_e) {
                            LOG_ERROR << "Transaction rollback failed: " << rb_e.what();
                        }
                    }
                    batch_failed = true;
                } catch (const std::exception& e) {
                    LOG_ERROR << "Copy batch transaction failed: " << e.what();
                    if (txn) {
                        try {
                            txn->rollback();
                        } catch (const std::exception& rb_e) {
                            LOG_ERROR << "Transaction rollback failed: " << rb_e.what();
                        }
                    }
                    batch_failed = true;
                }

                if (batch_failed) {
                    LOG_WARN << "Copy batch rolled back, skipping "
                             << valid_items.size() << " files in this chunk";
                }
            }
        }

        auto reserved_size = static_cast<int64_t>(total_copy_size);
        auto consumed_size = static_cast<int64_t>(actual_copy_size);
        auto release_size = reserved_size - consumed_size;
        if (release_size > 0) {
            co_await UpdateStorageUsed(m_db_client, user_id, -release_size);
        }

        LOG_INFO << "File copy completed: copied_count=" << copied_count
                 << ", total_size=" << actual_copy_size;

        CopyResponse response;
        response.copied_count = copied_count;
        response.new_files = new_files;
        co_return response;
    }

    // ==================== Delete ====================

    auto FileService::Delete(DeleteRequest request, uint64_t user_id)
        -> drogon::Task<Result<DeleteResponse>> {

        LOG_DEBUG << "Starting delete file: file_ids.size()=" << request.file_ids.size()
                  << ", user_id=" << user_id;

        int deleted_count = 0;

        auto chunks = BatchUtils::Chunk(request.file_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }

            auto in_clause = BatchUtils::BuildNumericInClause(chunk);

            std::unordered_map<uint64_t, Files> file_map;
            file_map.reserve(chunk.size());

            try {
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, " "       is_favorite, download_count, created_at, updated_at " "FROM files WHERE id IN (" + in_clause + ") AND user_id = ?",
                    user_id
                );

                for (const auto& row : result) {
                    auto file = Files(row);
                    file_map[file.getValueOfId()] = std::move(file);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "File batch fetch failed in delete, skipping chunk: " << e.base().what();
                for (const auto& file_id : chunk) {
                    LOG_WARN << "File not found or delete failed, skipping: file_id=" << file_id;
                }
                continue;
            }

            struct TrashItem {
                uint64_t file_id;
                std::string item_name;
                uint64_t item_size;
                uint64_t original_folder_id;
                std::string original_path;
                std::optional<uint64_t> content_id;
                std::string item_data;
            };

            std::vector<TrashItem> trash_items;
            trash_items.reserve(chunk.size());

            for (const auto& file_id : chunk) {
                auto it = file_map.find(file_id);
                if (it == file_map.end()) {
                    LOG_WARN << "File not found or delete failed, skipping: file_id=" << file_id;
                    continue;
                }

                const auto& file = it->second;

                Json::Value item_data;
                if (file.getContentId()) {
                    item_data["content_id"] = static_cast<Json::UInt64>(*file.getContentId());
                }
                item_data["mime_type"] = file.getValueOfMimeType();
                Json::StreamWriterBuilder builder;
                builder["indentation"] = "";

                trash_items.push_back({ .file_id = file.getValueOfId(), .item_name = file.getValueOfName(), .item_size = file.getValueOfSize(), .original_folder_id = file.getValueOfFolderId(), .original_path = file.getValueOfPath(), .content_id = file.getContentId() ? std::optional<uint64_t>(*file.getContentId()) : std::nullopt, .item_data = Json::writeString(builder, item_data) });
            }

            if (!trash_items.empty()) {
                // ── 构建回收站 SQL（事务外，仅字符串拼接） ──
                std::string trash_sql =
                    "INSERT INTO trash (user_id, item_type, item_id, item_name, item_size, " "content_id, original_folder_id, original_path, item_data, " "deleted_at, expires_at) VALUES ";

                for (size_t i = 0; i < trash_items.size(); ++i) {
                    if (i > 0) {
                        trash_sql += ",";
                    }
                    const auto& item = trash_items[i];

                    trash_sql += "(" + std::to_string(user_id) + ",'file',";
                    trash_sql += std::to_string(item.file_id) + ",";
                    trash_sql += "'" + EscapeSqlLiteral(item.item_name) + "',";
                    trash_sql += std::to_string(item.item_size) + ",";
                    if (item.content_id.has_value()) {
                        trash_sql += std::to_string(item.content_id.value());
                    } else {
                        trash_sql += "NULL";
                    }
                    trash_sql += "," + std::to_string(item.original_folder_id) + ",";
                    trash_sql += "'" + EscapeSqlLiteral(item.original_path) + "',";
                    trash_sql += "'" + EscapeSqlLiteral(item.item_data) + "',";
                    trash_sql += "NOW(),DATE_ADD(NOW(), INTERVAL 30 DAY))";
                }

                std::vector<uint64_t> deletable_ids;
                deletable_ids.reserve(trash_items.size());
                for (const auto& item : trash_items) {
                    deletable_ids.push_back(item.file_id);
                }

                // ── 事务阶段：回收站插入 + 文件删除，原子提交/回滚 ──
                std::shared_ptr<drogon::orm::Transaction> txn;
                bool batch_failed = false;
                try {
                    txn = co_await m_db_client->newTransactionCoro();

                    auto insert_ok = co_await InsertTrashRecords(txn, trash_sql);
                    if (!insert_ok) {
                        LOG_WARN << "Trash insert failed in transaction, rolling back batch";
                        batch_failed = true;
                    } else {
                        int deleted = co_await DeleteFilesByIds(txn, deletable_ids);
                        deleted_count += deleted;
                        for (const auto& file_id : deletable_ids) {
                            LOG_DEBUG << "File moved to trash: file_id=" << file_id;
                        }
                    }
                    // 事务自动提交（协程正常完成）
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_ERROR << "Delete batch transaction failed (DB): " << e.base().what();
                    if (txn) {
                        try {
                            txn->rollback();
                        } catch (const std::exception& rb_e) {
                            LOG_ERROR << "Transaction rollback failed: " << rb_e.what();
                        }
                    }
                    batch_failed = true;
                } catch (const std::exception& e) {
                    LOG_ERROR << "Delete batch transaction failed: " << e.what();
                    if (txn) {
                        try {
                            txn->rollback();
                        } catch (const std::exception& rb_e) {
                            LOG_ERROR << "Transaction rollback failed: " << rb_e.what();
                        }
                    }
                    batch_failed = true;
                }

                if (batch_failed) {
                    LOG_WARN << "Delete batch rolled back, skipping "
                             << trash_items.size() << " files in this chunk";
                }
            }
        }

        LOG_INFO << "File delete completed: deleted_count=" << deleted_count;

        DeleteResponse response;
        response.deleted_count = deleted_count;
        co_return response;
    }

    // ==================== Search ====================

    auto FileService::Search(SearchRequest request, uint64_t user_id)
        -> drogon::Task<Result<SearchResponse>> {

        LOG_DEBUG << "Starting search file: keyword=\"" << request.keyword
                  << "\", type=" << request.type << ", folder_id="
                  << (request.folder_id.has_value() ? std::to_string(*request.folder_id) : "null")
                  << ", page=" << request.page << ", page_size=" << request.page_size
                  << ", user_id=" << user_id;

        std::vector<SearchResultItem> items;
        int total = 0;
        int total_pages = 0;

        std::string search_pattern = "%" + request.keyword + "%";
        auto offset = (request.page - 1) * request.page_size;

        try {
            if (request.type == "all") {
                std::string count_sql =
                    "SELECT COUNT(*) FROM (" "  SELECT f.id, f.name, 'file' AS type " "  FROM files f " "  WHERE f.user_id = ? AND f.name LIKE ? " "  AND (? IS NULL OR f.folder_id = ?) " "  UNION ALL " "  SELECT fo.id, fo.name, 'folder' AS type " "  FROM folders fo " "  WHERE fo.user_id = ? AND fo.name LIKE ? " "  AND (? IS NULL OR fo.parent_id = ?) " ") AS combined";

                auto count_result = co_await m_db_client->execSqlCoro(
                    count_sql,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["COUNT(*)"].as<int64_t>());
                }

                std::string data_sql =
                    "SELECT * FROM (" "  SELECT f.id, f.name, 'file' AS type, f.size, f.mime_type, " "         COALESCE(fc.hash_md5, '') AS hash, 0 AS item_count, f.path, f.created_at, f.updated_at " "  FROM files f " "  LEFT JOIN file_contents fc ON f.content_id = fc.id " "  WHERE f.user_id = ? AND f.name LIKE ? " "  AND (? IS NULL OR f.folder_id = ?) " "  UNION ALL " "  SELECT fo.id, fo.name, 'folder' AS type, 0 AS size, '' AS mime_type, " "         '' AS hash, fo.item_count, fo.path, fo.created_at, fo.updated_at " "  FROM folders fo " "  WHERE fo.user_id = ? AND fo.name LIKE ? " "  AND (? IS NULL OR fo.parent_id = ?) " ") AS combined " "ORDER BY name ASC " "LIMIT ? OFFSET ?";

                auto result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.page_size,
                    offset
                );

                for (const auto& row : result) {
                    SearchResultItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = row["type"].as<std::string>();
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.path = row["path"].as<std::string>();
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "file") {
                std::string count_sql =
                    "SELECT COUNT(*) FROM files f " "WHERE f.user_id = ? AND f.name LIKE ? " "AND (? IS NULL OR f.folder_id = ?)";

                auto count_result = co_await m_db_client->execSqlCoro(
                    count_sql,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["COUNT(*)"].as<int64_t>());
                }

                std::string data_sql =
                    "SELECT f.id, f.name, f.size, f.mime_type, f.path, f.created_at, f.updated_at, " "       COALESCE(fc.hash_md5, '') AS hash " "FROM files f " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE f.user_id = ? AND f.name LIKE ? " "AND (? IS NULL OR f.folder_id = ?) " "ORDER BY name ASC " "LIMIT ? OFFSET ?";

                auto result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.page_size,
                    offset
                );

                for (const auto& row : result) {
                    SearchResultItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "file";
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.path = row["path"].as<std::string>();
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "folder") {
                std::string count_sql =
                    "SELECT COUNT(*) FROM folders fo " "WHERE fo.user_id = ? AND fo.name LIKE ? " "AND (? IS NULL OR fo.parent_id = ?)";

                auto count_result = co_await m_db_client->execSqlCoro(
                    count_sql,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["COUNT(*)"].as<int64_t>());
                }

                std::string data_sql =
                    "SELECT fo.id, fo.name, fo.item_count, fo.path, fo.created_at, fo.updated_at " "FROM folders fo " "WHERE fo.user_id = ? AND fo.name LIKE ? " "AND (? IS NULL OR fo.parent_id = ?) " "ORDER BY name ASC " "LIMIT ? OFFSET ?";

                auto result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    user_id,
                    search_pattern,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.folder_id.has_value() ? *request.folder_id : 0,
                    request.page_size,
                    offset
                );

                for (const auto& row : result) {
                    SearchResultItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "folder";
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.path = row["path"].as<std::string>();
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }
            }

            total_pages = request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Failed to search: " << e.base().what();
        }

        SearchResponse response;
        response.items = items;
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };

        LOG_DEBUG << "Search completed: total=" << total << ", page=" << request.page;
        co_return response;
    }

    // ==================== 私有辅助方法 ====================

    auto FileService::CheckStorageQuota(uint64_t user_id, uint64_t file_size) const
        -> drogon::Task<Result<void>> {
        co_return co_await CheckStorageQuota(m_db_client, user_id, file_size);
    }

    auto FileService::ReserveStorageQuota(uint64_t user_id, uint64_t file_size) const
        -> drogon::Task<Result<void>> {

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "UPDATE users SET storage_reserved = storage_reserved + ? " "WHERE id = ? AND storage_used + storage_reserved + ? <= storage_quota",
                file_size,
                user_id,
                file_size
            );

            if (result.affectedRows() == 0) {
                LOG_WARN << "Insufficient storage quota for reservation: user_id=" << user_id
                         << ", file_size=" << file_size;
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            LOG_DEBUG << "Storage quota reserved: user_id=" << user_id
                      << ", file_size=" << file_size;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to reserve storage quota: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto FileService::ReleaseReservedQuota(uint64_t user_id, uint64_t reserved_bytes)
        -> drogon::Task<void> {

        if (reserved_bytes == 0) {
            co_return;
        }

        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE users SET storage_reserved = GREATEST(storage_reserved - ?, 0) WHERE id = ?",
                reserved_bytes,
                user_id
            );

            LOG_DEBUG << "Reserved quota released: user_id=" << user_id
                      << ", reserved_bytes=" << reserved_bytes;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to release reserved quota: " << e.base().what();
        }
    }

    auto FileService::FindExistingContent(const std::string& file_hash) const
        -> drogon::Task<std::optional<uint64_t>> {

        try {
            CoroMapper<FileContents> mapper(m_db_client);
            auto content = co_await mapper.findOne(
                Criteria(FileContents::Cols::_hash_md5, CompareOperator::EQ, file_hash)
            );

            co_return content.getValueOfId();

        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
    }

    auto FileService::FindExistingTask(uint64_t user_id, const std::string& file_hash) const
        -> drogon::Task<std::optional<UploadTasks>> {

        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            auto task = co_await mapper.findOne(
                Criteria(UploadTasks::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(UploadTasks::Cols::_file_hash, CompareOperator::EQ, file_hash) &&
                Criteria(UploadTasks::Cols::_status, CompareOperator::EQ, 0)
            );

            co_return task;

        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
    }

    auto FileService::FindUploadTask(const std::string& upload_id, uint64_t user_id) const
        -> drogon::Task<Result<UploadTasks>> {

        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            auto task = co_await mapper.findByPrimaryKey(upload_id);

            if (task.getValueOfUserId() != user_id) {
                LOG_WARN << "Upload task does not belong to current user: upload_id=" << upload_id
                         << ", task_user_id=" << task.getValueOfUserId()
                         << ", request_user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
            }

            co_return task;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to query upload task: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
        }
    }

    auto FileService::BuildUploadTaskCacheEntry(const UploadTasks& task) -> UploadTaskCacheEntry {
        return UploadTaskCacheEntry{ .user_id = task.getValueOfUserId(),
                                     .total_chunks = task.getValueOfTotalChunks(),
                                     .expires_at = task.getValueOfExpiresAt(),
                                     .status = task.getValueOfStatus(),
                                     .file_hash = task.getValueOfFileHash(),
                                     .filename = task.getValueOfFilename(),
                                     .parent_id = task.getValueOfFolderId(),
                                     .cache_expires_at = std::chrono::steady_clock::now() + UPLOAD_TASK_CACHE_TTL };
    }

    auto FileService::TryGetUploadTaskCacheEntry(const std::string& upload_id, uint64_t user_id)
        -> std::optional<UploadTaskCacheEntry> {

        const auto now = std::chrono::steady_clock::now();
        {
            // 读路径使用共享锁，降低高频分片上传之间的锁竞争。
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

        // 仅在确认缓存过期后切换到写锁做清理，避免读路径长时间持有独占锁。
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

    auto FileService::CacheUploadTaskEntry(const std::string& upload_id, UploadTaskCacheEntry entry)
        -> void {

        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        m_upload_task_cache[upload_id] = std::move(entry);
    }

    auto FileService::InvalidateUploadTaskCache(const std::string& upload_id) -> void {
        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        m_upload_task_cache.erase(upload_id);
    }

    auto FileService::StartUploadTaskCacheMaintenance() -> void {
        if (auto* loop = drogon::app().getLoop(); loop != nullptr) {
            loop->runEvery(
                UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS,
                [this]() { EvictExpiredUploadTaskCacheEntries(); }
            );
            LOG_DEBUG << "Upload task cache maintenance timer started (interval="
                      << UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS << "s)";
        }
    }

    auto FileService::EvictExpiredUploadTaskCacheEntries() -> void {
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

    auto FileService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {
        co_await UpdateStorageUsed(m_db_client, user_id, delta);
    }

    auto FileService::IsFilenameExists(
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
            LOG_ERROR << "Failed to check filename: " << e.base().what();
            co_return false;
        }
    }

    auto FileService::ExtractExtension(const std::string& filename) -> std::string {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == filename.length() - 1) {
            return "";
        }
        return filename.substr(pos + 1);
    }

    auto FileService::IsImageMimeType(const std::string& mime_type) -> bool {
        return mime_type.starts_with("image/");
    }

    // ── 事务感知辅助方法实现 ──

    auto FileService::CheckStorageQuota(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t file_size
    ) const -> drogon::Task<Result<void>> {

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_used = storage_used + ? " "WHERE id = ? AND storage_used + ? <= storage_quota",
                file_size,
                user_id,
                file_size
            );

            if (result.affectedRows() == 0) {
                LOG_WARN << "Insufficient storage space: user_id=" << user_id
                         << ", file_size=" << file_size;
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            LOG_DEBUG << "Storage quota check passed and reserved: user_id=" << user_id
                      << ", file_size=" << file_size;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to reserve user storage quota: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto FileService::UpdateStorageUsed(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        int64_t delta
    ) -> drogon::Task<void> {

        try {
            if (delta >= 0) {
                auto result = co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = storage_used + ? " "WHERE id = ? AND storage_used + ? <= storage_quota",
                    delta,
                    user_id,
                    delta
                );

                if (result.affectedRows() == 0) {
                    LOG_WARN << "Skipped storage usage increment due to quota limit: user_id="
                             << user_id << ", delta=" << delta;
                    co_return;
                }
            } else {
                co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = GREATEST(storage_used + ?, 0) WHERE id = ?",
                    delta,
                    user_id
                );
            }

            LOG_DEBUG << "Storage usage updated: user_id=" << user_id << ", delta=" << delta;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to update storage usage: " << e.base().what();
        }
    }

    auto FileService::IncrementContentRefCount(
        const drogon::orm::DbClientPtr& client,
        const std::unordered_map<uint64_t, uint64_t>& content_ref_increment,
        const std::unordered_set<uint64_t>& existing_content_ids
    ) -> drogon::Task<std::unordered_set<uint64_t>> {

        std::string update_sql = "UPDATE file_contents SET ref_count = ref_count + CASE id ";
        std::vector<uint64_t> valid_content_ids;
        valid_content_ids.reserve(content_ref_increment.size());

        for (const auto& [content_id, increment] : content_ref_increment) {
            if (!existing_content_ids.contains(content_id)) {
                continue;
            }
            valid_content_ids.push_back(content_id);
            update_sql += " WHEN " + std::to_string(content_id) + " THEN " +
                          std::to_string(increment);
        }

        std::unordered_set<uint64_t> incremented_ids;
        incremented_ids.reserve(valid_content_ids.size());

        if (!valid_content_ids.empty()) {
            update_sql += " ELSE 0 END WHERE id IN (" +
                          BatchUtils::BuildNumericInClause(valid_content_ids) + ")";

            try {
                co_await client->execSqlCoro(update_sql);
                for (const auto id : valid_content_ids) {
                    incremented_ids.insert(id);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "File content batch ref_count update failed: " << e.base().what();
            }
        }

        co_return incremented_ids;
    }

    auto FileService::InsertCopiedFiles(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t target_folder_id,
        const std::vector<std::pair<uint64_t, const drogon_model::disk::Files*>>& valid_items
    ) -> drogon::Task<std::vector<std::pair<uint64_t, uint64_t>>> {

        std::vector<std::pair<uint64_t, uint64_t>> id_mappings;

        if (valid_items.empty()) {
            co_return id_mappings;
        }

        std::string insert_sql =
            "INSERT INTO files (user_id, content_id, folder_id, name, extension, " "size, mime_type, path, is_favorite, download_count) VALUES ";

        for (size_t i = 0; i < valid_items.size(); ++i) {
            if (i > 0) {
                insert_sql += ",";
            }
            const auto& file = *valid_items[i].second;
            auto content_id_ptr = file.getContentId();

            insert_sql += "(" + std::to_string(user_id) + ",";
            if (content_id_ptr) {
                insert_sql += std::to_string(*content_id_ptr);
            } else {
                insert_sql += "NULL";
            }
            insert_sql += "," + std::to_string(target_folder_id);
            insert_sql += ",'" + EscapeSqlLiteral(file.getValueOfName()) + "'";
            insert_sql += ",'" + EscapeSqlLiteral(file.getValueOfExtension()) + "'";
            insert_sql += "," + std::to_string(file.getValueOfSize());
            insert_sql += ",'" + EscapeSqlLiteral(file.getValueOfMimeType()) + "'";
            insert_sql += ",'',0,0)";
        }

        try {
            co_await client->execSqlCoro(insert_sql);

            auto id_result = co_await client->execSqlCoro("SELECT LAST_INSERT_ID() AS id");
            if (!id_result.empty()) {
                uint64_t first_id = id_result[0]["id"].as<uint64_t>();
                for (size_t i = 0; i < valid_items.size(); ++i) {
                    uint64_t new_id = first_id + i;
                    id_mappings.emplace_back(valid_items[i].first, new_id);
                }
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Batch file insert failed in copy: " << e.base().what();
        }

        co_return id_mappings;
    }

    auto FileService::InsertTrashRecords(
        const drogon::orm::DbClientPtr& client,
        const std::string& trash_sql
    ) -> drogon::Task<bool> {

        try {
            co_await client->execSqlCoro(trash_sql);
            co_return true;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Batch trash insert failed: " << e.base().what();
            co_return false;
        }
    }

    auto FileService::DeleteFilesByIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& file_ids
    ) -> drogon::Task<int> {

        if (file_ids.empty()) {
            co_return 0;
        }

        try {
            co_await client->execSqlCoro(
                "DELETE FROM files WHERE id IN (" +
                BatchUtils::BuildNumericInClause(file_ids) + ")"
            );
            co_return static_cast<int>(file_ids.size());
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Batch file delete failed: " << e.base().what();
            co_return 0;
        }
    }

} // namespace disk::file
