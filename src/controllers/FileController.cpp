/**
 * @file FileController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传控制器
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileController.hpp"

#include <fstream>

#include "dtos/FileDto.hpp"
#include "utils/Response.hpp"

namespace disk::file {

    FileController::FileController()
        : m_file_service(std::make_unique<FileService>(drogon::app().getDbClient())) {
        LOG_DEBUG << "FileController initialized";
    }

    auto FileController::InitUpload(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received initialize upload request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = InitUploadRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Initialize upload request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Initialize upload parameters validated: filename=\"" << parse_result->filename
                  << "\", file_size=" << parse_result->file_size
                  << ", parent_id=" << parse_result->parent_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层初始化上传
        auto result = co_await m_file_service->InitUpload(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "Initialize upload failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "Initialize upload successful: upload_id=" << result->upload_id
                 << ", instant_upload=" << result->instant_upload << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::UploadChunk(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received upload chunk request: " << request->getPeerAddr().toIpPort();

        // 1. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 2. 从 multipart/form-data 提取参数
        // 注意：UploadChunk 使用 multipart/form-data，而非 JSON
        const auto upload_id = std::string(request->getParameter("upload_id"));
        const auto chunk_index_str = std::string(request->getParameter("chunk_index"));
        const auto chunk_hash = std::string(request->getParameter("chunk_hash"));

        // 验证必填参数
        if (upload_id.empty()) {
            LOG_WARN << "Upload chunk request missing upload_id parameter";
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Missing upload_id parameter")
            );
        }
        if (chunk_index_str.empty()) {
            LOG_WARN << "Upload chunk request missing chunk_index parameter";
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Missing chunk_index parameter")
            );
        }
        if (chunk_hash.empty()) {
            LOG_WARN << "Upload chunk request missing chunk_hash parameter";
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Missing chunk_hash parameter")
            );
        }

        // 解析 chunk_index
        uint32_t chunk_index = 0;
        try {
            chunk_index = static_cast<uint32_t>(std::stoul(chunk_index_str));
        } catch (...) {
            LOG_WARN << "Invalid chunk_index format: " << chunk_index_str;
            co_return Response::Error(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid chunk_index format")
            );
        }

        // 验证 chunk_hash 格式（32位小写十六进制）
        if (chunk_hash.length() != 32) {
            LOG_WARN << "Invalid chunk_hash format: " << chunk_hash;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "chunk_hash must be 32-character lowercase hex string"
            ));
        }
        for (char c : chunk_hash) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                LOG_WARN << "Invalid chunk_hash format: " << chunk_hash;
                co_return Response::Error(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "chunk_hash must be 32-character lowercase hex string"
                ));
            }
        }

        // 3. 获取分片数据（从请求体）
        const auto& chunk_data = request->body();
        if (chunk_data.empty()) {
            LOG_WARN << "Upload chunk request missing chunk data";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "Missing chunk data"));
        }

        LOG_DEBUG << "Upload chunk parameters validated: upload_id=" << upload_id
                  << ", chunk_index=" << chunk_index << ", chunk_size=" << chunk_data.size();

        // 4. 调用 Service 层上传分片
        auto result =
            co_await m_file_service
                ->UploadChunk(upload_id, chunk_index, chunk_hash, std::string(chunk_data), user_id);
        if (!result) {
            LOG_ERROR << "Upload chunk failed: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
            co_return Response::Error(result.error());
        }

        // 5. 构造响应
        LOG_INFO << "Upload chunk successful: chunk_index=" << result->chunk_index
                 << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::CompleteUpload(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received complete upload request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CompleteUploadRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Complete upload request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Complete upload parameters validated: upload_id=" << parse_result->upload_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层完成上传
        auto result = co_await m_file_service->CompleteUpload(parse_result->upload_id, user_id);
        if (!result) {
            LOG_ERROR << "Complete upload failed: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << parse_result->upload_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "Complete upload successful: file_id=" << result->file.id << ", filename=\""
                 << result->file.name << "\""
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::CancelUpload(drogon::HttpRequestPtr request, std::string upload_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received cancel upload request: " << request->getPeerAddr().toIpPort()
                 << ", upload_id=" << upload_id;

        // 1. 验证 upload_id 非空
        if (upload_id.empty()) {
            LOG_WARN << "取消上传请求缺少 upload_id";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "缺少 upload_id"));
        }

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层取消上传
        auto result = co_await m_file_service->CancelUpload(upload_id, user_id);
        if (!result) {
            LOG_ERROR << "取消上传失败: " << result.error().message << " (user_id=" << user_id
                      << ", upload_id=" << upload_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 返回成功响应
        LOG_INFO << "取消上传成功: upload_id=" << upload_id << " (user_id=" << user_id << ")";
        co_return Response::Success({});
    }

    auto FileController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到获取文件列表请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = FileListRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "获取文件列表请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "获取文件列表参数验证通过: parent_id=" << parse_result->parent_id
                  << ", page=" << parse_result->page << ", page_size=" << parse_result->page_size
                  << ", sort_by=" << parse_result->sort_by
                  << ", sort_order=" << parse_result->sort_order << ", type=" << parse_result->type;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取文件列表
        auto result = co_await m_file_service->GetFileList(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "获取文件列表失败: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "获取文件列表成功: items=" << result->items.size() << " (user_id=" << user_id
                 << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::GetDetail(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到获取文件详情请求: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        // 1. 解析并验证路径参数
        auto parse_result = DownloadInfoRequest::FromPath(file_id);
        if (!parse_result) {
            LOG_WARN << "获取文件详情请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "获取文件详情参数验证通过: file_id=" << parse_result->file_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取文件详情（复用 GetDownloadInfo）
        auto result = co_await m_file_service->GetDownloadInfo(parse_result->file_id, user_id);
        if (!result) {
            LOG_ERROR << "获取文件详情失败: " << result.error().message << " (user_id=" << user_id
                      << ", file_id=" << file_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        Json::Value data;
        data["file"] = result->ToJson();

        LOG_INFO << "获取文件详情成功: filename=" << result->filename << " (user_id=" << user_id
                 << ", file_id=" << file_id << ")";
        co_return Response::Success(data);
    }

    auto FileController::DownloadInfo(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到获取下载信息请求: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        // 1. 解析并验证路径参数
        auto parse_result = disk::file::DownloadInfoRequest::FromPath(file_id);
        if (!parse_result) {
            LOG_WARN << "获取下载信息请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "获取下载信息参数验证通过: file_id=" << parse_result->file_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取下载信息
        auto result = co_await m_file_service->GetDownloadInfo(parse_result->file_id, user_id);
        if (!result) {
            LOG_ERROR << "获取下载信息失败: " << result.error().message << " (user_id=" << user_id
                      << ", file_id=" << file_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "获取下载信息成功: filename=" << result->filename
                 << ", size=" << result->file_size << " (user_id=" << user_id
                 << ", file_id=" << file_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Download(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到下载文件请求: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        // 1. 解析并验证路径参数
        auto parse_result = disk::file::DownloadRequest::FromPath(file_id);
        if (!parse_result) {
            LOG_WARN << "下载文件请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "下载文件参数验证通过: file_id=" << parse_result->file_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 获取下载文件信息
        auto info_result = co_await m_file_service->GetDownloadData(parse_result->file_id, user_id);
        if (!info_result) {
            LOG_ERROR << "获取下载数据失败: " << info_result.error().message
                      << " (user_id=" << user_id << ", file_id=" << file_id << ")";
            co_return Response::Error(info_result.error());
        }

        const auto& download_info = *info_result;
        LOG_INFO << "获取下载信息成功: file_id=" << file_id
                 << ", filename=" << download_info.filename << ", size=" << download_info.file_size
                 << ", storage_path=" << download_info.storage_path;

        // 4. 检查文件是否存在
        if (!std::filesystem::exists(download_info.storage_path)) {
            LOG_ERROR << "文件不存在: " << download_info.storage_path;
            co_return Response::Error(ErrorInfo(ErrorCode::FileNotFound, "文件不存在"));
        }

        // 5. 解析 Range 请求头
        auto range_header = std::string(request->getHeader("Range"));
        auto range_request = RangeRequest::Parse(range_header, download_info.file_size);

        // 6. 处理 Range 请求
        if (range_request.has_range && !range_request.satisfiable) {
            LOG_WARN << "Range 请求无法满足: " << range_header
                     << ", file_size=" << download_info.file_size;

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::HttpStatusCode::k416RequestedRangeNotSatisfiable);
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

            std::string content_range = "bytes */" + std::to_string(download_info.file_size);
            resp->addHeader("Content-Range", content_range);

            Json::Value error_data;
            error_data["file_size"] = static_cast<Json::UInt64>(download_info.file_size);
            error_data["requested_range"] = range_header;
            error_data["reason"] = "请求的起始位置超出文件大小";

            Json::Value body;
            body["code"] = 10002;
            body["message"] = "请求范围无效";
            body["data"] = error_data;

            resp->setBody(body.toStyledString());
            co_return resp;
        }

        // 7. 读取文件内容
        std::ifstream file(download_info.storage_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR << "无法打开文件: " << download_info.storage_path;
            co_return Response::Error(ErrorInfo(ErrorCode::FileNotFound, "无法打开文件"));
        }

        uint64_t start = range_request.has_range ? range_request.start : 0;
        uint64_t end = range_request.has_range ? range_request.end : download_info.file_size - 1;
        uint64_t content_length = end - start + 1;

        file.seekg(static_cast<std::streampos>(start));
        std::string content(content_length, '\0');
        file.read(content.data(), static_cast<std::streamsize>(content_length));
        file.close();

        // 8. 构建响应
        auto resp = drogon::HttpResponse::newHttpResponse();

        if (range_request.has_range) {
            resp->setStatusCode(drogon::HttpStatusCode::k206PartialContent);
            std::string content_range = "bytes " + std::to_string(start) + "-" +
                                        std::to_string(end) + "/" +
                                        std::to_string(download_info.file_size);
            resp->addHeader("Content-Range", content_range);
            LOG_INFO << "返回部分内容: start=" << start << ", end=" << end
                     << ", total=" << download_info.file_size;
        } else {
            resp->setStatusCode(drogon::HttpStatusCode::k200OK);
            LOG_INFO << "返回完整文件: size=" << download_info.file_size;
        }

        // 设置响应头
        resp->setContentTypeString(download_info.mime_type);
        resp->addHeader("Content-Length", std::to_string(content_length));
        resp->addHeader("Accept-Ranges", "bytes");

        std::string disposition = "attachment; filename=\"" + download_info.filename + "\"";
        resp->addHeader("Content-Disposition", disposition);

        if (!download_info.file_hash.empty()) {
            resp->addHeader("ETag", "\"" + download_info.file_hash + "\"");
        }

        resp->setBody(std::move(content));

        co_return resp;
    }

    auto FileController::Rename(drogon::HttpRequestPtr request, std::string file_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到重命名请求: " << request->getPeerAddr().toIpPort()
                 << ", file_id=" << file_id;

        // 1. 解析并验证路径参数和请求体
        auto parse_result = RenameRequest::FromPathAndRequest(file_id, request);
        if (!parse_result) {
            LOG_WARN << "重命名请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "重命名参数验证通过: file_id=" << parse_result->file_id << ", new_name=\""
                  << parse_result->new_name << "\"";

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层重命名文件
        auto result = co_await m_file_service->Rename(
            parse_result->file_id,
            std::move(parse_result->new_name),
            user_id
        );
        if (!result) {
            LOG_ERROR << "重命名失败: " << result.error().message << " (user_id=" << user_id
                      << ", file_id=" << file_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "重命名成功: file_id=" << file_id << ", new_name=\"" << result->name << "\""
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Move(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到移动文件请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = MoveRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "移动文件请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "移动文件参数验证通过: file_ids.size()=" << parse_result->file_ids.size()
                  << ", target_folder_id=" << parse_result->target_folder_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层移动文件
        auto result = co_await m_file_service->Move(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "移动文件失败: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "移动文件成功: moved_count=" << result->moved_count << " (user_id=" << user_id
                 << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Copy(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到复制文件请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CopyRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "复制文件请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "复制文件参数验证通过: file_ids.size()=" << parse_result->file_ids.size()
                  << ", target_folder_id=" << parse_result->target_folder_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层复制文件
        auto result = co_await m_file_service->Copy(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "复制文件失败: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "复制文件成功: copied_count=" << result->copied_count << " (user_id=" << user_id
                 << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Delete(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到删除文件请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = DeleteRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "删除文件请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "删除文件参数验证通过: file_ids.size()=" << parse_result->file_ids.size();

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层删除文件
        auto result = co_await m_file_service->Delete(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "删除文件失败: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "删除文件成功: deleted_count=" << result->deleted_count
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::Search(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到文件搜索请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = SearchRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "文件搜索请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "文件搜索参数验证通过: keyword=\"" << parse_result->keyword << "\"";

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层搜索文件
        auto result = co_await m_file_service->Search(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "文件搜索失败: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "文件搜索成功: total=" << result->pagination.total
                 << ", page=" << result->pagination.page << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

} // namespace disk::file
