/**
 * @file FileService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务实现
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileService.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <drogon/utils/Utilities.h>
#include <json/writer.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/Users.hpp"
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
    using drogon_model::disk::UploadTasks;
    using drogon_model::disk::Users;

    // ==================== 构造函数 ====================

    FileService::FileService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        LOG_DEBUG << "FileService initialization completed";
    }

    // ==================== InitUpload ====================

    auto FileService::InitUpload(InitUploadRequest request, uint64_t user_id)
        -> drogon::Task<Result<InitUploadResponse>> {

        LOG_DEBUG << "Starting initialize upload: filename=\"" << request.filename
                  << "\", file_size=" << request.file_size << ", file_hash=" << request.file_hash
                  << ", parent_id=" << request.parent_id << ", user_id=" << user_id;

        // 1. 检查存储配额
        auto quota_result = co_await CheckStorageQuota(user_id, request.file_size);
        if (!quota_result) {
            LOG_WARN << "Storage quota check failed: user_id=" << user_id;
            co_return std::unexpected(quota_result.error());
        }

        // 2. 检测秒传：查找已存在的内容
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
                CoroMapper<FileContents> content_mapper(m_db_client);
                auto content = co_await content_mapper.findOne(
                    Criteria(FileContents::Cols::_id, CompareOperator::EQ, existing_content.value())
                );

                // 增加引用计数
                auto ref_count = content.getValueOfRefCount() + 1;
                content.setRefCount(ref_count);
                co_await content_mapper.update(content);

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

        // 3. 检测断点续传
        auto existing_task = co_await FindExistingTask(user_id, request.file_hash);
        if (existing_task.has_value()) {
            const auto& task = existing_task.value();
            LOG_INFO << "Resume upload check successful: upload_id=" << task.getValueOfId()
                     << ", uploaded_chunks=" << task.getValueOfUploadedChunks();

            InitUploadResponse response;
            response.upload_id = task.getValueOfId();
            response.chunk_size = task.getValueOfChunkSize();
            response.total_chunks = task.getValueOfTotalChunks();
            response.uploaded_chunks = {};
            response.instant_upload = false;

            // 解析已上传分片
            auto chunks = ParseUploadedChunks(task.getValueOfUploadedChunks());
            response.uploaded_chunks = std::vector<uint32_t>(chunks.begin(), chunks.end());

            co_return response;
        }

        // 4. 创建新的上传任务
        auto config = ConfigMgr::GetInstance();
        auto chunk_size = config->GetChunkSize();
        auto total_chunks = static_cast<uint32_t>(
            std::ceil(static_cast<double>(request.file_size) / static_cast<double>(chunk_size))
        );
        auto expiry_seconds = config->GetUploadTaskExpirySeconds();
        auto temp_path = GetTempDirPath(drogon::utils::getUuid()).string();

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
        task.setUploadedChunks("[]");
        task.setTempPath(GetTempDirPath(upload_id).string());
        task.setStatus(0); // 进行中
        task.setExpiresAt(trantor::Date::now().after(expiry_seconds));

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
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create upload task")
            );
        }
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
        auto temp_dir = GetTempDirPath(upload_id);
        std::error_code ec;

        if (!std::filesystem::exists(temp_dir)) {
            if (!std::filesystem::create_directories(temp_dir, ec)) {
                LOG_ERROR << "Failed to create temp directory: " << temp_dir << " - "
                          << ec.message();
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create temp directory")
                );
            }
        }

        auto chunk_file_path = GetChunkFilePath(upload_id, chunk_index);
        std::ofstream chunk_file(chunk_file_path, std::ios::binary);
        if (!chunk_file) {
            LOG_ERROR << "Failed to open chunk file: " << chunk_file_path;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to open chunk file")
            );
        }

        chunk_file.write(chunk_data.data(), chunk_data.size());
        chunk_file.close();

        if (!chunk_file) {
            LOG_ERROR << "Failed to write chunk file: " << chunk_file_path;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to write chunk file")
            );
        }

        // 6. 更新已上传分片列表
        auto uploaded_chunks = ParseUploadedChunks(task.getValueOfUploadedChunks());
        uploaded_chunks.insert(chunk_index);

        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            auto updated_task = task;
            updated_task.setUploadedChunks(SerializeUploadedChunks(uploaded_chunks));
            co_await mapper.update(updated_task);

            LOG_DEBUG << "Chunk upload successful: upload_id=" << upload_id
                      << ", chunk_index=" << chunk_index;

            UploadChunkResponse response;
            response.chunk_index = chunk_index;
            response.uploaded = true;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to update upload task: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to update upload task")
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

        // 2. 验证所有分片已上传
        auto uploaded_chunks = ParseUploadedChunks(task.getValueOfUploadedChunks());
        if (uploaded_chunks.size() != task.getValueOfTotalChunks()) {
            LOG_WARN << "Not all chunks uploaded: uploaded=" << uploaded_chunks.size()
                     << ", total=" << task.getValueOfTotalChunks();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
            );
        }

        // 3. 组装分片
        auto assemble_path = GetAssembleFilePath(upload_id);
        std::ofstream assemble_file(assemble_path, std::ios::binary);
        if (!assemble_file) {
            LOG_ERROR << "Failed to create assemble file: " << assemble_path;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create assemble file")
            );
        }

        std::array<char, 8192> buffer{};
        for (uint32_t i = 0; i < task.getValueOfTotalChunks(); ++i) {
            auto chunk_path = GetChunkFilePath(upload_id, i);
            std::ifstream chunk_file(chunk_path, std::ios::binary);
            if (!chunk_file) {
                LOG_ERROR << "Failed to open chunk file: " << chunk_path;
                assemble_file.close();
                std::filesystem::remove(assemble_path);
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to open chunk file")
                );
            }

            while (chunk_file.read(buffer.data(), buffer.size())) {
                assemble_file.write(buffer.data(), chunk_file.gcount());
            }
            if (chunk_file.gcount() > 0) {
                assemble_file.write(buffer.data(), chunk_file.gcount());
            }
            chunk_file.close();
        }
        assemble_file.close();

        if (!assemble_file) {
            LOG_ERROR << "Failed to write assemble file: " << assemble_path;
            std::filesystem::remove(assemble_path);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to write assemble file")
            );
        }

        // 4. 计算并验证最终 MD5
        auto hash_result = FileHashUtil::HashFileMd5(assemble_path);
        if (!hash_result) {
            LOG_ERROR << "Failed to compute file MD5";
            std::filesystem::remove(assemble_path);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to compute file hash")
            );
        }

        const auto& final_hash = hash_result.value();
        if (final_hash != task.getValueOfFileHash()) {
            LOG_ERROR << "File hash mismatch: expected=" << task.getValueOfFileHash()
                      << ", actual=" << final_hash;
            std::filesystem::remove(assemble_path);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "File hash verification failed")
            );
        }

        LOG_DEBUG << "File hash verification passed: " << final_hash;

        // 5. 检查去重
        auto existing_content = co_await FindExistingContent(final_hash);
        uint64_t content_id = 0;

        try {
            if (existing_content.has_value()) {
                // 已存在，增加引用计数
                content_id = existing_content.value();
                CoroMapper<FileContents> content_mapper(m_db_client);
                auto content = co_await content_mapper.findOne(
                    Criteria(FileContents::Cols::_id, CompareOperator::EQ, content_id)
                );
                content.setRefCount(content.getValueOfRefCount() + 1);
                co_await content_mapper.update(content);

                // 删除临时组装文件
                std::filesystem::remove(assemble_path);

                LOG_DEBUG << "File dedup successful: content_id=" << content_id;

            } else {
                // 创建新的 FileContents 记录
                auto final_storage_path = GetFinalStoragePath(final_hash);
                auto final_dir = final_storage_path.parent_path();

                std::error_code ec;
                if (!std::filesystem::exists(final_dir)) {
                    std::filesystem::create_directories(final_dir, ec);
                }

                // 移动临时文件到最终存储位置
                std::filesystem::rename(assemble_path, final_storage_path, ec);
                if (ec) {
                    LOG_ERROR << "Failed to move file: " << ec.message();
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to move file")
                    );
                }

                // 计算 SHA256
                auto sha256_result = FileHashUtil::HashFileSha256(final_storage_path);
                if (!sha256_result) {
                    LOG_ERROR << "Failed to compute SHA256";
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to compute SHA256")
                    );
                }

                // 创建 FileContents 记录
                FileContents content;
                content.setHashMd5(final_hash);
                content.setHashSha256(sha256_result.value());
                content.setSize(task.getValueOfFileSize());
                content.setStoragePath(final_storage_path.string());
                content.setMimeType(""); // MIME 类型可由控制器层推断
                content.setRefCount(1);

                CoroMapper<FileContents> content_mapper(m_db_client);
                content = co_await content_mapper.insert(content);
                content_id = content.getValueOfId();

                LOG_DEBUG << "FileContents created successfully: content_id=" << content_id;
            }

            // 6. 检查同名文件
            if (co_await IsFilenameExists(
                    task.getValueOfFolderId(),
                    task.getValueOfFilename(),
                    user_id
                )) {
                LOG_WARN << "File with same name already exists: " << task.getValueOfFilename();
                // 回滚：减少引用计数或删除内容
                co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
            }

            // 7. 创建 Files 记录
            Files file;
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

            CoroMapper<Files> file_mapper(m_db_client);
            file = co_await file_mapper.insert(file);

            LOG_INFO << "Files record created successfully: file_id=" << file.getValueOfId();

            // 8. 更新用户存储使用量
            co_await UpdateStorageUsed(user_id, static_cast<int64_t>(task.getValueOfFileSize()));

            // 9. 删除上传任务和临时目录
            try {
                CoroMapper<UploadTasks> task_mapper(m_db_client);
                co_await task_mapper.deleteByPrimaryKey(task.getValueOfId());

                auto temp_dir = GetTempDirPath(upload_id);
                std::filesystem::remove_all(temp_dir);

                LOG_DEBUG << "Upload task cleanup completed: " << upload_id;
            } catch (const std::exception& e) {
                LOG_WARN << "Failed to cleanup upload task (non-critical): " << e.what();
            }

            // 10. 返回响应
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

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Database operation failed: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Database operation failed")
            );
        }
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

        const auto& task = task_result.value();

        // 2. 删除临时目录
        auto temp_dir = GetTempDirPath(upload_id);
        std::error_code ec;

        if (std::filesystem::exists(temp_dir)) {
            if (std::filesystem::remove_all(temp_dir, ec) == 0U) {
                LOG_WARN << "Failed to delete temp directory: " << temp_dir << " - "
                         << ec.message();
            } else {
                LOG_DEBUG << "Temp directory deleted: " << temp_dir;
            }
        }

        // 3. 删除上传任务记录
        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            co_await mapper.deleteByPrimaryKey(upload_id);

            LOG_INFO << "Upload task cancelled and deleted: upload_id=" << upload_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to delete upload task: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to delete upload task")
            );
        }
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

        // 2. 查询文件和文件夹
        std::vector<FileListItem> all_items;

        // 查询文件
        if (request.type == "all" || request.type == "file") {
            try {
                CoroMapper<Files> file_mapper(m_db_client);
                auto file_criteria =
                    Criteria(Files::Cols::_folder_id, CompareOperator::EQ, request.parent_id) &&
                    Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id);
                auto files = co_await file_mapper.findBy(file_criteria);

                // 获取文件内容的 hash 信息
                for (const auto& file : files) {
                    FileListItem item;
                    item.id = file.getValueOfId();
                    item.name = file.getValueOfName();
                    item.type = "file";
                    item.size = file.getValueOfSize();
                    item.mime_type = file.getValueOfMimeType();
                    item.created_at = file.getValueOfCreatedAt().toDbStringLocal();
                    item.updated_at = file.getValueOfUpdatedAt().toDbStringLocal();

                    // 获取 hash
                    if (file.getContentId()) {
                        try {
                            CoroMapper<FileContents> content_mapper(m_db_client);
                            auto content = co_await content_mapper.findOne(Criteria(
                                FileContents::Cols::_id,
                                CompareOperator::EQ,
                                *file.getContentId()
                            ));
                            item.hash = content.getValueOfHashMd5();
                        } catch (const drogon::orm::DrogonDbException&) {
                            item.hash = "";
                        }
                    }

                    all_items.push_back(item);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "Failed to query file list: " << e.base().what();
            }
        }

        // 查询文件夹
        if (request.type == "all" || request.type == "folder") {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                auto folder_criteria =
                    Criteria(Folders::Cols::_parent_id, CompareOperator::EQ, request.parent_id) &&
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id);
                auto folders = co_await folder_mapper.findBy(folder_criteria);

                for (const auto& folder : folders) {
                    FileListItem item;
                    item.id = folder.getValueOfId();
                    item.name = folder.getValueOfName();
                    item.type = "folder";
                    item.item_count = static_cast<int>(folder.getValueOfItemCount());
                    item.created_at = folder.getValueOfCreatedAt().toDbStringLocal();
                    item.updated_at = folder.getValueOfUpdatedAt().toDbStringLocal();
                    all_items.push_back(item);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "Failed to query folder list: " << e.base().what();
            }
        }

        // 3. 排序
        auto sort_comparator = [&request](const FileListItem& a, const FileListItem& b) -> bool {
            bool result = false;
            if (request.sort_by == "name") {
                result = a.name < b.name;
            } else if (request.sort_by == "size") {
                result = a.size < b.size;
            } else if (request.sort_by == "created_at") {
                result = a.created_at < b.created_at;
            } else if (request.sort_by == "updated_at") {
                result = a.updated_at < b.updated_at;
            }
            return request.sort_order == "desc" ? !result : result;
        };
        std::sort(all_items.begin(), all_items.end(), sort_comparator);

        // 4. 分页
        auto total = static_cast<int>(all_items.size());
        auto offset = (request.page - 1) * request.page_size;
        auto total_pages =
            request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        std::vector<FileListItem> paginated_items;
        for (int i = offset; i < std::min(offset + request.page_size, total); ++i) {
            paginated_items.push_back(all_items[i]);
        }

        // 5. 构造响应
        FileListResponse response;
        response.items = paginated_items;
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

        Users user;
        try {
            CoroMapper<Users> user_mapper(m_db_client);
            user = co_await user_mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to query user info: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to query user info")
            );
        }

        auto storage_used = user.getValueOfStorageUsed();
        auto storage_quota = user.getValueOfStorageQuota();

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
                LOG_WARN << "文件不存在或无权限，跳过: file_id=" << file_id;
            }
        }

        if (storage_used + total_copy_size > storage_quota) {
            LOG_WARN << "存储空间不足: used=" << storage_used << ", quota=" << storage_quota
                     << ", copy_size=" << total_copy_size;
            co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
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
                    LOG_WARN << "目标文件夹已存在同名文件，跳过: " << file.getValueOfName();
                    continue;
                }

                auto content_id_ptr = file.getContentId();
                if (content_id_ptr) {
                    auto content = co_await content_mapper.findOne(
                        Criteria(FileContents::Cols::_id, CompareOperator::EQ, *content_id_ptr)
                    );
                    content.setRefCount(content.getValueOfRefCount() + 1);
                    co_await content_mapper.update(content);
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

                LOG_DEBUG << "文件复制成功: old_id=" << old_id
                          << ", new_id=" << new_file.getValueOfId();

            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_ERROR << "复制文件失败: file_id=" << old_id << " - " << e.base().what();
            }
        }

        if (copied_count > 0) {
            co_await UpdateStorageUsed(user_id, static_cast<int64_t>(actual_copy_size));
        }

        LOG_INFO << "文件复制完成: copied_count=" << copied_count
                 << ", total_size=" << actual_copy_size;

        CopyResponse response;
        response.copied_count = copied_count;
        response.new_files = new_files;
        co_return response;
    }

    // ==================== Delete ====================

    auto FileService::Delete(DeleteRequest request, uint64_t user_id)
        -> drogon::Task<Result<DeleteResponse>> {

        LOG_DEBUG << "开始删除文件: file_ids.size()=" << request.file_ids.size()
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
                LOG_DEBUG << "文件移入回收站: file_id=" << file_id;

            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "文件不存在或删除失败，跳过: file_id=" << file_id << " - "
                         << e.base().what();
            }
        }

        LOG_INFO << "文件删除完成: deleted_count=" << deleted_count;

        DeleteResponse response;
        response.deleted_count = deleted_count;
        co_return response;
    }

    // ==================== Search ====================

    auto FileService::Search(SearchRequest request, uint64_t user_id)
        -> drogon::Task<Result<SearchResponse>> {

        LOG_DEBUG << "开始搜索文件: keyword=\"" << request.keyword << "\", type=" << request.type
                  << ", folder_id="
                  << (request.folder_id.has_value() ? std::to_string(*request.folder_id) : "null")
                  << ", page=" << request.page << ", page_size=" << request.page_size
                  << ", user_id=" << user_id;

        std::vector<SearchResultItem> all_items;

        // 构建搜索模式（LIKE %keyword%）
        std::string search_pattern = "%" + request.keyword + "%";

        // 搜索文件
        if (request.type == "all" || request.type == "file") {
            try {
                CoroMapper<Files> file_mapper(m_db_client);
                auto file_criteria =
                    Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id) &&
                    Criteria(Files::Cols::_name, CompareOperator::Like, search_pattern);

                // 如果指定了 folder_id，限定搜索范围
                if (request.folder_id.has_value()) {
                    file_criteria =
                        file_criteria &&
                        Criteria(Files::Cols::_folder_id, CompareOperator::EQ, *request.folder_id);
                }

                auto files = co_await file_mapper.findBy(file_criteria);

                for (const auto& file : files) {
                    SearchResultItem item;
                    item.id = file.getValueOfId();
                    item.name = file.getValueOfName();
                    item.type = "file";
                    item.size = file.getValueOfSize();
                    item.mime_type = file.getValueOfMimeType();
                    item.path = file.getValueOfPath();
                    item.created_at = file.getValueOfCreatedAt().toDbStringLocal();
                    item.updated_at = file.getValueOfUpdatedAt().toDbStringLocal();

                    // 获取 hash
                    if (file.getContentId()) {
                        try {
                            CoroMapper<FileContents> content_mapper(m_db_client);
                            auto content = co_await content_mapper.findOne(Criteria(
                                FileContents::Cols::_id,
                                CompareOperator::EQ,
                                *file.getContentId()
                            ));
                            item.hash = content.getValueOfHashMd5();
                        } catch (const drogon::orm::DrogonDbException&) {
                            item.hash = "";
                        }
                    }

                    all_items.push_back(item);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "搜索文件失败: " << e.base().what();
            }
        }

        // 搜索文件夹
        if (request.type == "all" || request.type == "folder") {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                auto folder_criteria =
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id) &&
                    Criteria(Folders::Cols::_name, CompareOperator::Like, search_pattern);

                // 如果指定了 folder_id，限定搜索范围
                if (request.folder_id.has_value()) {
                    folder_criteria = folder_criteria && Criteria(
                                                             Folders::Cols::_parent_id,
                                                             CompareOperator::EQ,
                                                             *request.folder_id
                                                         );
                }

                auto folders = co_await folder_mapper.findBy(folder_criteria);

                for (const auto& folder : folders) {
                    SearchResultItem item;
                    item.id = folder.getValueOfId();
                    item.name = folder.getValueOfName();
                    item.type = "folder";
                    item.item_count = static_cast<int>(folder.getValueOfItemCount());
                    item.path = folder.getValueOfPath();
                    item.created_at = folder.getValueOfCreatedAt().toDbStringLocal();
                    item.updated_at = folder.getValueOfUpdatedAt().toDbStringLocal();
                    all_items.push_back(item);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "搜索文件夹失败: " << e.base().what();
            }
        }

        // 排序（按名称排序）
        std::sort(
            all_items.begin(),
            all_items.end(),
            [](const SearchResultItem& a, const SearchResultItem& b) { return a.name < b.name; }
        );

        // 分页
        auto total = static_cast<int>(all_items.size());
        auto offset = (request.page - 1) * request.page_size;
        auto total_pages =
            request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        std::vector<SearchResultItem> paginated_items;
        for (int i = offset; i < std::min(offset + request.page_size, total); ++i) {
            paginated_items.push_back(all_items[i]);
        }

        // 构造响应
        SearchResponse response;
        response.items = paginated_items;
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };

        LOG_DEBUG << "搜索完成: total=" << total << ", page=" << request.page;
        co_return response;
    }

    // ==================== 私有辅助方法 ====================

    auto FileService::CheckStorageQuota(uint64_t user_id, uint64_t file_size) const
        -> drogon::Task<Result<void>> {

        try {
            CoroMapper<Users> mapper(m_db_client);
            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));

            auto storage_used = user.getValueOfStorageUsed();
            auto storage_quota = user.getValueOfStorageQuota();

            if (storage_used + file_size > storage_quota) {
                LOG_WARN << "存储空间不足: used=" << storage_used << ", quota=" << storage_quota
                         << ", file_size=" << file_size;
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            LOG_DEBUG << "存储配额检查通过: used=" << storage_used << ", quota=" << storage_quota;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "查询用户存储配额失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "查询存储配额失败"));
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
                LOG_WARN << "上传任务不属于当前用户: upload_id=" << upload_id
                         << ", task_user_id=" << task.getValueOfUserId()
                         << ", request_user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
            }

            co_return task;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "查询上传任务失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
        }
    }

    auto FileService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {

        try {
            CoroMapper<Users> mapper(m_db_client);
            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));

            auto new_used = static_cast<int64_t>(user.getValueOfStorageUsed()) + delta;
            new_used = std::max<int64_t>(new_used, 0);

            user.setStorageUsed(static_cast<uint64_t>(new_used));
            co_await mapper.update(user);

            LOG_DEBUG << "存储使用量已更新: user_id=" << user_id << ", delta=" << delta
                      << ", new_used=" << new_used;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "更新存储使用量失败: " << e.base().what();
        }
    }

    auto FileService::ParseUploadedChunks(const std::string& uploaded_chunks_json)
        -> std::set<uint32_t> {
        std::set<uint32_t> result;

        if (uploaded_chunks_json.empty() || uploaded_chunks_json == "[]") {
            return result;
        }

        Json::Value root;
        Json::Reader reader;

        if (reader.parse(uploaded_chunks_json, root) && root.isArray()) {
            for (const auto& item : root) {
                if (item.isUInt()) {
                    result.insert(item.asUInt());
                }
            }
        }

        return result;
    }

    auto FileService::SerializeUploadedChunks(const std::set<uint32_t>& chunks) -> std::string {
        Json::Value root(Json::arrayValue);

        for (const auto& chunk : chunks) {
            root.append(chunk);
        }

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, root);
    }

    auto FileService::GetChunkFilePath(const std::string& upload_id, uint32_t chunk_index) const
        -> std::filesystem::path {

        return GetTempDirPath(upload_id) / (std::to_string(chunk_index) + ".chunk");
    }

    auto FileService::GetTempDirPath(const std::string& upload_id) const -> std::filesystem::path {
        auto config = ConfigMgr::GetInstance();
        return std::filesystem::path(config->GetTempUploadPath()) / upload_id;
    }

    auto FileService::GetAssembleFilePath(const std::string& upload_id) const
        -> std::filesystem::path {
        auto config = ConfigMgr::GetInstance();
        return std::filesystem::path(config->GetTempUploadPath()) / (upload_id + ".tmp");
    }

    auto FileService::GetFinalStoragePath(const std::string& file_hash) const
        -> std::filesystem::path {
        auto config = ConfigMgr::GetInstance();
        // 使用 hash 的前 2 个字符作为子目录，避免单个目录文件过多
        auto hash_prefix = file_hash.substr(0, 2);
        return std::filesystem::path(config->GetStorageBasePath()) / hash_prefix /
               (file_hash + ".bin");
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
            LOG_ERROR << "检查文件名失败: " << e.base().what();
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
        return mime_type.find("image/") == 0;
    }

} // namespace disk::file
