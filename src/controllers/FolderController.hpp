/**
 * @file FolderController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹控制器
 * @note Request 和 Response DTO 定义在 dtos/FolderDto.hpp
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/FolderService.hpp"

namespace disk::folder {

    // ==================== Controller ====================

    class FolderController : public drogon::HttpController<FolderController> {
    public:
        FolderController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            FolderController::CreateFolder,
            "/api/folder/create",
            drogon::Post,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            FolderController::GetTree,
            "/api/folder/tree",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            FolderController::GetBreadcrumb,
            "/api/folder/{folder_id}/breadcrumb",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
        );
        METHOD_LIST_END

        /**
         * @brief 创建文件夹
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto CreateFolder(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 获取文件夹树
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto GetTree(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 获取面包屑路径
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto GetBreadcrumb(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<FolderService> m_folder_service;
    };

} // namespace disk::folder
