/**
 * @file FileService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileService.hpp"

#include <cmath>

#include <drogon/utils/Utilities.h>
#include <json/writer.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/UploadTaskChunks.hpp"
#include "storage/IFileStorage.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"

namespace disk::file {

    using disk::utils::ConfigMgr;
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

    // ==================== 构造函数 ====================

    FileService::FileService(drogon::orm::DbClientPtr db_client, storage::IFileStorage* storage)
        : m_db_client(std::move(db_client)), m_storage(storage) {
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
        std::string chunk_data,
        uint64_t user_id
    ) -> drogon::Task<Result<UploadChunkResponse>> {

        LOG_DEBUG << "Starting upload chunk: upload_id=" << upload_id
                  << ", chunk_index=" << chunk_index << ", chunk_hash=" << chunk_hash
                  << ", data_size=" << chunk_data.size();

        // 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            LOG_WARN << "Upload task verification failed: " << upload_id;
            co_return std::unexpected(task_result.error());
        }

        const auto& task = task_result.value();

        // 2. 验证任务未过期
        if (task.getValueOfExpiresAt() < trantor::Date::now()) {
            LOG_WARN << "Upload task expired: " << upload_id;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::UploadTaskNotFound, "Upload task expired")
            );
        }

        // 3. 验证分片索引有效
        if (chunk_index >= task.getValueOfTotalChunks()) {
            LOG_WARN << "Chunk index out of range: chunk_index=" << chunk_index
                     << ", total_chunks=" << task.getValueOfTotalChunks();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Chunk index out of range")
            );
        }

        // 4. 验证分片哈希
        auto actual_hash = FileHashUtil::HashMd5(chunk_data);
        if (actual_hash != chunk_hash) {
            LOG_WARN << "Chunk hash mismatch: expected=" << chunk_hash
                     << ", actual=" << actual_hash;
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

            UploadChunkResponse response;
            response.chunk_index = chunk_index;
            response.uploaded = true;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to record chunk upload: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to record chunk upload")
            );
        }
    }

    // ==================== CompleteUpload ====================

    auto FileService::CompleteUpload(std::string upload_id, uint64_t user_id)
        -> drogon::Task<Result<CompleteUploadResponse>> {

        LOG_DEBUG << "Starting complete upload: upload_id=" << upload_id << ", user_id=" << user_id;

        // 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            LOG_WARN << "Upload task verification failed: " << upload_id;
            co_return std::unexpected(task_result.error());
        }

        auto task = task_result.value();

        // 2. Check idempotency: already completed
        if (task.getValueOfStatus() == 1) {
            LOG_INFO << "Upload task already completed: upload_id=" << upload_id;
            // Return success without re-running file creation flow
            co_return CompleteUploadResponse{};
        }

        // 3. 验证所有分片已上传
        auto chunk_count_result = co_await m_db_client->execSqlCoro(
            "SELECT COUNT(*) AS cnt FROM upload_task_chunks WHERE task_id = ?",
            upload_id
        );
        auto uploaded_count = chunk_count_result.empty() ? 0 : static_cast<size_t>(chunk_count_result[0]["cnt"].as<int64_t>());

        if (uploaded_count != task.getValueOfTotalChunks()) {
            LOG_WARN << "Not all chunks uploaded: uploaded=" << uploaded_count
                     << ", total=" << task.getValueOfTotalChunks();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
            );
        }

        // 3. 组装分片
        auto assemble_result = co_await m_storage->AssembleChunks(upload_id, task.getValueOfTotalChunks());
        if (!assemble_result) {
            LOG_ERROR << "Failed to assemble chunks: upload_id=" << upload_id
                      << ", error=" << static_cast<int>(assemble_result.error().code);
            co_return std::unexpected(assemble_result.error());
        }
        const auto& assemble_path = assemble_result.value();

        // 4. 计算并验证最终 MD5
        auto hash_result = FileHashUtil::HashFileMd5(assemble_path);
        if (!hash_result) {
            LOG_ERROR << "Failed to compute file MD5";
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                LOG_WARN << "Failed to cleanup assemble file after md5 failure: "
                         << static_cast<int>(delete_result.error().code);
            }
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to compute file hash")
            );
        }

        const auto& final_hash = hash_result.value();
        if (final_hash != task.getValueOfFileHash()) {
            LOG_ERROR << "File hash mismatch: expected=" << task.getValueOfFileHash()
                      << ", actual=" << final_hash;
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                LOG_WARN << "Failed to cleanup assemble file after hash mismatch: "
                         << static_cast<int>(delete_result.error().code);
            }
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
                co_return std::unexpected(promote_result.error());
            }

            final_storage_path = promote_result.value();

            auto sha256_result = FileHashUtil::HashFileSha256(final_storage_path);
            if (!sha256_result) {
                LOG_ERROR << "Failed to compute SHA256";
                auto delete_result = co_await m_storage->DeletePath(final_storage_path);
                if (!delete_result) {
                    LOG_WARN << "Failed to cleanup final file after sha256 failure: "
                             << static_cast<int>(delete_result.error().code);
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to compute SHA256")
                );
            }

            final_sha256 = sha256_result.value();
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
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Database operation failed")
            );
        }

        LOG_INFO << "Files record created successfully: file_id=" << file.getValueOfId();

        // 转移预留配额为实际使用量
        try {
            auto transfer_result = co_await m_db_client->execSqlCoro(
                "UPDATE users SET storage_reserved = GREATEST(storage_reserved - ?, 0), " "storage_used = storage_used + ? WHERE id = ?",
                task.getValueOfFileSize(),
                task.getValueOfFileSize(),
                user_id
            );

            if (transfer_result.affectedRows() == 0) {
                LOG_WARN << "Failed to transfer reserved quota to used: user_id=" << user_id;
            } else {
                LOG_DEBUG << "Quota transferred: reserved -> used, user_id=" << user_id
                          << ", bytes=" << task.getValueOfFileSize();
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Failed to transfer reserved quota: " << e.base().what();
        }

        // 6. Set terminal state (status=1 completed) and cleanup chunk tracking rows
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE upload_tasks SET status = 1, finalized_at = NOW() WHERE id = ? AND status = 0",
                upload_id
            );

            co_await m_db_client->execSqlCoro(
                "DELETE FROM upload_task_chunks WHERE task_id = ?",
                upload_id
            );

            LOG_DEBUG << "Upload task finalized: " << upload_id;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Failed to finalize upload task (non-critical): " << e.base().what();
        }

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

        co_return response;
    }

    // ==================== CancelUpload ====================

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
        CoroMapper<Files> file_mapper(m_db_client);

        for (const auto& file_id : request.file_ids) {
            try {
                auto file = co_await file_mapper.findOne(
                    Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                    Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
                );

                if (file.getValueOfFolderId() == request.target_folder_id) {
                    LOG_DEBUG << "File already in target folder, skipping: file_id=" << file_id;
                    ++moved_count;
                    continue;
                }

                if (co_await IsFilenameExists(
                        request.target_folder_id,
                        file.getValueOfName(),
                        user_id
                    )) {
                    LOG_WARN << "Target folder already has file with same name, skipping: "
                             << file.getValueOfName();
                    continue;
                }

                file.setFolderId(request.target_folder_id);
                file.setUpdatedAt(trantor::Date::now());
                co_await file_mapper.update(file);

                ++moved_count;
                LOG_DEBUG << "File move successful: file_id=" << file_id;

            } catch (const drogon::orm::DrogonDbException&) {
                LOG_WARN << "File not found or move failed, skipping: file_id=" << file_id;
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
        CoroMapper<Files> file_mapper(m_db_client);
        std::vector<std::pair<uint64_t, Files>> files_to_copy;

        for (const auto& file_id : request.file_ids) {
            try {
                auto file = co_await file_mapper.findOne(
                    Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                    Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
                );
                total_copy_size += file.getValueOfSize();
                files_to_copy.emplace_back(file_id, file);
            } catch (const drogon::orm::DrogonDbException&) {
                LOG_WARN << "File not found or no permission, skipping: file_id=" << file_id;
            }
        }

        if (total_copy_size == 0) {
            LOG_INFO << "No files can be copied after validation";
            CopyResponse response;
            response.copied_count = 0;
            response.new_files = {};
            co_return response;
        }

        auto quota_result = co_await CheckStorageQuota(user_id, total_copy_size);
        if (!quota_result) {
            LOG_WARN << "Storage quota check failed for copy: user_id=" << user_id
                     << ", total_copy_size=" << total_copy_size;
            co_return std::unexpected(quota_result.error());
        }

        int copied_count = 0;
        uint64_t actual_copy_size = 0;
        std::vector<FileIdMapping> new_files;
        CoroMapper<FileContents> content_mapper(m_db_client);

        for (const auto& [old_id, file] : files_to_copy) {
            try {
                if (co_await IsFilenameExists(
                        request.target_folder_id,
                        file.getValueOfName(),
                        user_id
                    )) {
                    LOG_WARN << "Target folder already has file with same name, skipping: "
                             << file.getValueOfName();
                    continue;
                }

                auto content_id_ptr = file.getContentId();
                if (content_id_ptr) {
                    auto increment_result = co_await m_db_client->execSqlCoro(
                        "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = ?",
                        *content_id_ptr
                    );
                    if (increment_result.affectedRows() == 0) {
                        LOG_WARN << "File content not found during copy: content_id="
                                 << *content_id_ptr;
                        continue;
                    }
                }

                Files new_file;
                new_file.setUserId(user_id);
                if (content_id_ptr) {
                    new_file.setContentId(*content_id_ptr);
                }
                new_file.setFolderId(request.target_folder_id);
                new_file.setName(file.getValueOfName());
                new_file.setExtension(file.getValueOfExtension());
                new_file.setSize(file.getValueOfSize());
                new_file.setMimeType(file.getValueOfMimeType());
                new_file.setPath("");
                new_file.setIsFavorite(0);
                new_file.setDownloadCount(0);

                new_file = co_await file_mapper.insert(new_file);

                ++copied_count;
                actual_copy_size += file.getValueOfSize();
                new_files.push_back({ .old_id = old_id, .new_id = new_file.getValueOfId() });

                LOG_DEBUG << "File copy successful: old_id=" << old_id
                          << ", new_id=" << new_file.getValueOfId();

            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_ERROR << "Failed to copy file: file_id=" << old_id << " - " << e.base().what();
            }
        }

        auto reserved_size = static_cast<int64_t>(total_copy_size);
        auto consumed_size = static_cast<int64_t>(actual_copy_size);
        auto release_size = reserved_size - consumed_size;
        if (release_size > 0) {
            co_await UpdateStorageUsed(user_id, -release_size);
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
        CoroMapper<Files> file_mapper(m_db_client);
        CoroMapper<Trash> trash_mapper(m_db_client);

        for (const auto& file_id : request.file_ids) {
            try {
                auto file = co_await file_mapper.findOne(
                    Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                    Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
                );

                Trash trash;
                trash.setUserId(user_id);
                trash.setItemType("file");
                trash.setItemId(file.getValueOfId());
                trash.setItemName(file.getValueOfName());
                trash.setItemSize(file.getValueOfSize());
                trash.setOriginalFolderId(file.getValueOfFolderId());
                trash.setOriginalPath(file.getValueOfPath());

                Json::Value item_data;
                if (file.getContentId()) {
                    item_data["content_id"] = static_cast<Json::UInt64>(*file.getContentId());
                    trash.setContentId(*file.getContentId());
                }
                item_data["mime_type"] = file.getValueOfMimeType();
                Json::StreamWriterBuilder builder;
                builder["indentation"] = "";
                trash.setItemData(Json::writeString(builder, item_data));

                auto now = trantor::Date::now();
                trash.setDeletedAt(now);
                trash.setExpiresAt(now.after(30 * 24 * 60 * 60));

                co_await trash_mapper.insert(trash);
                co_await file_mapper.deleteByPrimaryKey(file.getValueOfId());

                ++deleted_count;
                LOG_DEBUG << "File moved to trash: file_id=" << file_id;

            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "File not found or delete failed, skipping: file_id=" << file_id
                         << " - " << e.base().what();
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

        try {
            auto result = co_await m_db_client->execSqlCoro(
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

    auto FileService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {

        try {
            if (delta >= 0) {
                auto result = co_await m_db_client->execSqlCoro(
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
                co_await m_db_client->execSqlCoro(
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

} // namespace disk::file
