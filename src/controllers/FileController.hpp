/**
 * @file FileController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传控制器
 * @note Request 和 Response DTO 定义在 dtos/FileDto.hpp
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/FileService.hpp"

namespace disk::file {

    // ==================== Controller ====================

    class FileController : public drogon::HttpController<FileController> {
    public:
        FileController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            FileController::InitUpload,
            "/api/file/upload/init",
            drogon::Post,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::UploadChunk,
            "/api/file/upload/chunk",
            drogon::Post,
            "JwtAuthFilter",
            "UploadRateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::CompleteUpload,
            "/api/file/upload/complete",
            drogon::Post,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::CancelUpload,
            "/api/file/upload/{upload_id}",
            drogon::Delete,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::List,
            "/api/file/list",
            drogon::Get,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::DownloadInfo,
            "/api/file/download/{file_id}/info",
            drogon::Get,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::Download,
            "/api/file/download/{file_id}",
            drogon::Get,
            "JwtAuthFilter"
        );
        ADD_METHOD_TO(
            FileController::Rename,
            "/api/file/{file_id}/rename",
            drogon::Put,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::Move,
            "/api/file/move",
            drogon::Put,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::Copy,
            "/api/file/copy",
            drogon::Post,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::Delete,
            "/api/file",
            drogon::Delete,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        ADD_METHOD_TO(
            FileController::Search,
            "/api/file/search",
            drogon::Get,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        METHOD_LIST_END

        /**
         * @brief 初始化上传
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto InitUpload(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 上传分片
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto UploadChunk(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 完成上传
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto CompleteUpload(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 取消上传
         * @param request HTTP请求对象
         * @param upload_id 上传会话ID
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto CancelUpload(drogon::HttpRequestPtr request, std::string upload_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 获取文件列表
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto List(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 获取下载信息
         * @param request HTTP请求对象
         * @param file_id 文件ID
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto DownloadInfo(drogon::HttpRequestPtr request, std::string file_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 下载文件
         * @param request HTTP请求对象
         * @param file_id 文件ID
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应（支持 Range 请求）
         */
        [[nodiscard]]
        auto Download(drogon::HttpRequestPtr request, std::string file_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 重命名文件
         * @param request HTTP请求对象
         * @param file_id 文件ID（路径参数）
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Rename(drogon::HttpRequestPtr request, std::string file_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 移动文件
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Move(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 复制文件
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Copy(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 删除文件
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Delete(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 搜索文件
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Search(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<FileService> m_file_service;
    };

} // namespace disk::file
