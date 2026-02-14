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

#include "dtos/FileDto.hpp"
#include "utils/Response.hpp"

namespace disk::file {

    FileController::FileController()
        : m_file_service(std::make_unique<FileService>(drogon::app().getDbClient())) {
        LOG_DEBUG << "FileController 初始化完成";
    }

    auto FileController::InitUpload(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到初始化上传请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = InitUploadRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "初始化上传请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "初始化上传参数验证通过: filename=\"" << parse_result->filename
                  << "\", file_size=" << parse_result->file_size
                  << ", parent_id=" << parse_result->parent_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层初始化上传
        auto result = co_await m_file_service->InitUpload(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "初始化上传失败: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "初始化上传成功: upload_id=" << result->upload_id
                 << ", instant_upload=" << result->instant_upload
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::UploadChunk(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到上传分片请求: " << request->getPeerAddr().toIpPort();

        // 1. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 2. 从 multipart/form-data 提取参数
        // 注意：UploadChunk 使用 multipart/form-data，而非 JSON
        const auto upload_id = std::string(request->getParameter("upload_id"));
        const auto chunk_index_str = std::string(request->getParameter("chunk_index"));
        const auto chunk_hash = std::string(request->getParameter("chunk_hash"));

        // 验证必填参数
        if (upload_id.empty()) {
            LOG_WARN << "上传分片请求缺少 upload_id 参数";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "缺少 upload_id 参数"));
        }
        if (chunk_index_str.empty()) {
            LOG_WARN << "上传分片请求缺少 chunk_index 参数";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "缺少 chunk_index 参数"));
        }
        if (chunk_hash.empty()) {
            LOG_WARN << "上传分片请求缺少 chunk_hash 参数";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "缺少 chunk_hash 参数"));
        }

        // 解析 chunk_index
        uint32_t chunk_index = 0;
        try {
            chunk_index = static_cast<uint32_t>(std::stoul(chunk_index_str));
        } catch (...) {
            LOG_WARN << "chunk_index 格式无效: " << chunk_index_str;
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "chunk_index 格式无效"));
        }

        // 验证 chunk_hash 格式（32位小写十六进制）
        if (chunk_hash.length() != 32) {
            LOG_WARN << "chunk_hash 格式错误: " << chunk_hash;
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "chunk_hash 必须是 32 位小写十六进制字符串"));
        }
        for (char c : chunk_hash) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                LOG_WARN << "chunk_hash 格式错误: " << chunk_hash;
                co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "chunk_hash 必须是 32 位小写十六进制字符串"));
            }
        }

        // 3. 获取分片数据（从请求体）
        const auto& chunk_data = request->body();
        if (chunk_data.empty()) {
            LOG_WARN << "上传分片请求缺少分片数据";
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "缺少分片数据"));
        }

        LOG_DEBUG << "上传分片参数验证通过: upload_id=" << upload_id
                  << ", chunk_index=" << chunk_index
                  << ", chunk_size=" << chunk_data.size();

        // 4. 调用 Service 层上传分片
        auto result = co_await m_file_service->UploadChunk(
            upload_id,
            chunk_index,
            chunk_hash,
            std::string(chunk_data),
            user_id
        );
        if (!result) {
            LOG_ERROR << "上传分片失败: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
            co_return Response::Error(result.error());
        }

        // 5. 构造响应
        LOG_INFO << "上传分片成功: chunk_index=" << result->chunk_index
                 << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::CompleteUpload(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到完成上传请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CompleteUploadRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "完成上传请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "完成上传参数验证通过: upload_id=" << parse_result->upload_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层完成上传
        auto result = co_await m_file_service->CompleteUpload(parse_result->upload_id, user_id);
        if (!result) {
            LOG_ERROR << "完成上传失败: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << parse_result->upload_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "完成上传成功: file_id=" << result->file.id
                 << ", filename=\"" << result->file.name << "\""
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FileController::CancelUpload(drogon::HttpRequestPtr request, std::string upload_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到取消上传请求: " << request->getPeerAddr().toIpPort()
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
            LOG_ERROR << "取消上传失败: " << result.error().message
                      << " (user_id=" << user_id << ", upload_id=" << upload_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 返回成功响应
        LOG_INFO << "取消上传成功: upload_id=" << upload_id
                 << " (user_id=" << user_id << ")";
        co_return Response::Success({});
    }

} // namespace disk::file
