/**
 * @file FileController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileController.hpp"

#include <algorithm>
#include <limits>
#include <memory>

#include "DownloadResponder.hpp"
#include "dtos/FileDto.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/Response.hpp"

namespace disk::file {

    FileController::FileController()
        : m_file_service(std::make_unique<FileService>(drogon::app().getDbClient(), storage::StorageMgr::GetStorage())),
          m_storage(storage::StorageMgr::GetStorage()) {
    }

    auto FileController::InitUpload(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Debug() << "Received initialize upload request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = InitUploadRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Initialize upload request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Initialize upload parameters validated: filename=\"" << parse_result->filename
                  << "\", file_size=" << parse_result->file_size
                  << ", parent_id=" << parse_result->parent_id;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层初始化上传
        auto result = co_await m_file_service->InitUpload(*parse_result, user_id);
        if (!result) {
            Logger::Error() << "Initialize upload failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Debug() << "Initialize upload successful: upload_id=" << result->upload_id
                  << ", instant_upload=" << result->instant_upload << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::UploadChunk(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Debug() << "Received upload chunk request: " << request->getPeerAddr().toIpPort();

        /// 1. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 2. 从查询参数提取元数据
        /// 注意：UploadChunk 使用查询参数 (upload_id, chunk_index, chunk_hash) + 原始二进制请求体 (application/octet-stream)
        const auto upload_id = std::string(request->getParameter("upload_id"));
        const auto chunk_index_str = std::string(request->getParameter("chunk_index"));
        const auto chunk_hash = std::string(request->getParameter("chunk_hash"));

        /// 验证必填参数
        if (upload_id.empty()) {
            Logger::Warn() << "Upload chunk request missing upload_id parameter";
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Missing upload_id parameter")
            );
        }
        if (chunk_index_str.empty()) {
            Logger::Warn() << "Upload chunk request missing chunk_index parameter";
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Missing chunk_index parameter")
            );
        }
        if (chunk_hash.empty()) {
            Logger::Warn() << "Upload chunk request missing chunk_hash parameter";
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Missing chunk_hash parameter")
            );
        }

        /// 解析 chunk_index
        uint32_t chunk_index = 0;
        try {
            chunk_index = static_cast<uint32_t>(std::stoul(chunk_index_str));
        } catch (...) {
            Logger::Warn() << "Invalid chunk_index format: " << chunk_index_str;
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid chunk_index format")
            );
        }

        /// 验证 chunk_hash 格式（32位小写十六进制）
        if (chunk_hash.length() != 32) {
            Logger::Warn() << "Invalid chunk_hash format: " << chunk_hash;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "chunk_hash must be 32-character lowercase hex string"
            ));
        }
        for (char c : chunk_hash) {
            if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
                Logger::Warn() << "Invalid chunk_hash format: " << chunk_hash;
                co_return Response::Error(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "chunk_hash must be 32-character lowercase hex string"
                ));
            }
        }

        /// 3. 获取分片数据（从请求体）
        std::string_view chunk_data = request->body();
        if (chunk_data.empty()) {
            Logger::Warn() << "Upload chunk request missing chunk data";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "Missing chunk data"));
        }

        Logger::Debug() << "Upload chunk parameters validated: upload_id=" << upload_id
                  << ", chunk_index=" << chunk_index << ", chunk_size=" << chunk_data.size();

        /// 4. 调用 Service 层上传分片
        auto result =
            co_await m_file_service
                ->UploadChunk(upload_id, chunk_index, chunk_hash, chunk_data, user_id);
        if (!result) {
            Logger::Error() << "Upload chunk failed: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
            co_return Response::Error(result.error());
        }

        /// 5. 构造响应
        Logger::Debug() << "Upload chunk successful: chunk_index=" << result->chunk_index
                  << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::CompleteUpload(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Debug() << "Received complete upload request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = CompleteUploadRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Complete upload request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Complete upload parameters validated: upload_id=" << parse_result->upload_id;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层完成上传
        auto result = co_await m_file_service->CompleteUpload(parse_result->upload_id, user_id);
        if (!result) {
            Logger::Error() << "Complete upload failed: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << parse_result->upload_id
                      << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Debug() << "Complete upload successful: file_id=" << result->file.id << ", filename=\""
                  << result->file.name << "\""
                  << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::CancelUpload(drogon::HttpRequestPtr request, std::string upload_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Debug() << "Received cancel upload request: " << request->getPeerAddr().toIpPort()
                  << ", upload_id=" << upload_id;

        /// 1. 验证 upload_id 非空
        if (upload_id.empty()) {
            Logger::Warn() << "Cancel upload request missing upload_id";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "Missing upload_id"));
        }

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层取消上传
        auto result = co_await m_file_service->CancelUpload(upload_id, user_id);
        if (!result) {
            Logger::Error() << "Cancel upload failed: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 返回成功响应
        Logger::Debug() << "Cancel upload successful: upload_id=" << upload_id << " (user_id=" << user_id
                  << ")";
        co_return Response::Success({});
    }

    auto FileController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received file list request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = FileListRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "File list request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "File list parameters validated: parent_id=" << parse_result->parent_id
                  << ", page=" << parse_result->page << ", page_size=" << parse_result->page_size
                  << ", sort_by=" << parse_result->sort_by
                  << ", sort_order=" << parse_result->sort_order << ", type=" << parse_result->type;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层获取文件列表
        auto result = co_await m_file_service->GetFileList(*parse_result, user_id);
        if (!result) {
            Logger::Error() << "Get file list failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info() << "Get file list successful: items=" << result->items.size()
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::GetDetail(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received get file detail request: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        /// 1. 解析并验证路径参数
        auto parse_result = DownloadInfoRequest::FromPath(file_id);
        if (!parse_result) {
            Logger::Warn() << "Get file detail request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Get file detail parameters validated: file_id=" << parse_result->file_id;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层获取文件详情
        auto result = co_await m_file_service->GetFileDetail(parse_result->file_id, user_id);
        if (!result) {
            Logger::Error() << "Get file detail failed: " << result.error().message
                      << " (user_id=" << user_id << ", file_id=" << file_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info() << "Get file detail successful: name=" << result->name
                 << " (user_id=" << user_id << ", file_id=" << file_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::DownloadInfo(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received get download info request: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        /// 1. 解析并验证路径参数
        auto parse_result = disk::file::DownloadInfoRequest::FromPath(file_id);
        if (!parse_result) {
            Logger::Warn() << "Get download info request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Get download info parameters validated: file_id=" << parse_result->file_id;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层获取下载信息
        auto result = co_await m_file_service->GetDownloadInfo(parse_result->file_id, user_id);
        if (!result) {
            Logger::Error() << "Get download info failed: " << result.error().message
                      << " (user_id=" << user_id << ", file_id=" << file_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info() << "Get download info successful: filename=" << result->filename
                 << ", size=" << result->file_size << " (user_id=" << user_id
                 << ", file_id=" << file_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Download(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received download file request: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        /// 1. 解析并验证路径参数
        auto parse_result = disk::file::DownloadRequest::FromPath(file_id);
        if (!parse_result) {
            Logger::Warn() << "Download file request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Download file parameters validated: file_id=" << parse_result->file_id;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 获取下载文件信息
        auto info_result = co_await m_file_service->GetDownloadData(parse_result->file_id, user_id);
        if (!info_result) {
            Logger::Error() << "Get download data failed: " << info_result.error().message
                      << " (user_id=" << user_id << ", file_id=" << file_id << ")";
            co_return Response::Error(info_result.error());
        }

        const auto& download_info = *info_result;
        Logger::Info() << "Get download info successful: file_id=" << file_id
                 << ", filename=" << download_info.filename << ", size=" << download_info.file_size
                 << ", storage_path=" << download_info.storage_path;

        /// 4. 委托下载响应构造
        co_return co_await BuildDownloadResponse(
            disk::controllers::DownloadParams{
                .storage_path = download_info.storage_path,
                .filename = download_info.filename,
                .file_size = download_info.file_size,
                .mime_type = download_info.mime_type,
                .file_hash = download_info.file_hash,
                .range_header = std::string(request->getHeader("Range")),
            },
            m_storage
        );
    }

    auto FileController::Rename(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received rename request: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        /// 1. 解析并验证路径参数和请求体
        auto parse_result = RenameRequest::FromPathAndRequest(file_id, request);
        if (!parse_result) {
            Logger::Warn() << "Rename request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Rename parameters validated: file_id=" << parse_result->file_id
                  << ", new_name=\"" << parse_result->new_name << "\"";

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层重命名文件
        auto result = co_await m_file_service->Rename(
            parse_result->file_id,
            std::move(parse_result->new_name),
            user_id
        );
        if (!result) {
            Logger::Error() << "Rename failed: " << result.error().message << " (user_id=" << user_id
                      << ", file_id=" << file_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info() << "Rename successful: file_id=" << file_id << ", new_name=\"" << result->name
                 << "\""
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Move(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received move file request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = MoveRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Move file request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Move file parameters validated: file_ids.size()="
                  << parse_result->file_ids.size()
                  << ", target_folder_id=" << parse_result->target_folder_id;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层移动文件
        auto result = co_await m_file_service->Move(*parse_result, user_id);
        if (!result) {
            Logger::Error() << "Move file failed: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info() << "Move file successful: moved_count=" << result->moved_count
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Copy(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received copy file request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = CopyRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Copy file request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Copy file parameters validated: file_ids.size()="
                  << parse_result->file_ids.size()
                  << ", target_folder_id=" << parse_result->target_folder_id;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层复制文件
        auto result = co_await m_file_service->Copy(*parse_result, user_id);
        if (!result) {
            Logger::Error() << "Copy file failed: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info() << "Copy file successful: copied_count=" << result->copied_count
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Delete(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received delete file request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = DeleteRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Delete file request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Delete file parameters validated: file_ids.size()="
                  << parse_result->file_ids.size()
                  << ", folder_ids.size()=" << parse_result->folder_ids.size();

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层删除文件（异常边界：防止未捕获异常导致连接中断）
        try {
            auto result = co_await m_file_service->Delete(*parse_result, user_id);
            if (!result) {
                Logger::Error() << "Delete file failed: " << result.error().message << " (user_id=" << user_id
                          << ")";
                co_return Response::Error(result.error());
            }

            /// 4. 构造响应
            Logger::Info() << "Delete file successful: deleted_count=" << result->deleted_count
                     << ", deleted_file_count=" << result->deleted_file_count
                     << ", deleted_folder_count=" << result->deleted_folder_count
                     << " (user_id=" << user_id << ")";
            co_return Response::Success(result->ToJson());
        } catch (const std::exception& e) {
            Logger::Error() << "Unexpected exception in delete file: " << e.what()
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(
                ErrorInfo(ErrorCode::InternalError, "Internal error during file deletion")
            );
        }
    }

    auto FileController::Search(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received file search request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = SearchRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "File search request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "File search parameters validated: keyword=\"" << parse_result->keyword
                  << "\"";

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 3. 调用 Service 层搜索文件
        auto result = co_await m_file_service->Search(*parse_result, user_id);
        if (!result) {
            Logger::Error() << "File search failed: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info() << "File search successful: total=" << result->pagination.total
                 << ", page=" << result->pagination.page << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

} ///< namespace disk::file
