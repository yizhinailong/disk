/**
 * @file ShareController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享控制器
 * @note Request 和 Response DTO 定义在 dtos/ShareDto.hpp
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/ShareService.hpp"

namespace disk::share {

    // ==================== Controller ====================

    /**
     * @brief 分享控制器
     *
     * @details
     * 提供分享模块的 HTTP 接口：
     * - POST   /api/share                      - 创建分享（需 JWT）
     * - GET    /api/share                      - 获取分享列表（需 JWT）
     * - GET    /api/share/{share_id}           - 获取分享详情（需 JWT）
     * - PUT    /api/share/{share_id}           - 更新分享设置（需 JWT）
     * - DELETE /api/share                      - 批量取消分享（需 JWT）
     * - POST   /api/share/access/{share_id}    - 验证分享访问（公开）
     * - GET    /api/share/browse/{share_id}    - 浏览分享内容（需分享令牌）
     * - GET    /api/share/download/{share_id}/{file_id} - 下载文件（需分享令牌）
     *
     * 认证边界：
     * - 所有者端点（create/list/detail/update/cancel）：全局 JWT 过滤器
     * - 访问端点（access）：公开，无需认证
     * - 浏览/下载端点（browse/download）：ShareAuthFilter（X-Share-Token）
     */
    class ShareController : public drogon::HttpController<ShareController> {
    public:
        ShareController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            ShareController::Create,
            "/api/share",
            drogon::Post,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            ShareController::List,
            "/api/share",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            ShareController::Detail,
            "/api/share/{share_id}",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            ShareController::Update,
            "/api/share/{share_id}",
            drogon::Put,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            ShareController::Cancel,
            "/api/share",
            drogon::Delete,
            "disk::filters::JwtAuthFilter",
        );

        ADD_METHOD_TO(ShareController::Access, "/api/share/access/{share_id}", drogon::Post);

        ADD_METHOD_TO(
            ShareController::Browse,
            "/api/share/browse/{share_id}",
            drogon::Get,
            "disk::filters::ShareAuthFilter"
        );
        ADD_METHOD_TO(
            ShareController::Download,
            "/api/share/download/{share_id}/{file_id}",
            drogon::Get,
            "disk::filters::ShareAuthFilter"
        );
        METHOD_LIST_END

        /**
         * @brief 创建分享
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Create(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 获取分享列表
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto List(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 获取分享详情
         * @param request HTTP请求对象
         * @param share_id 分享码（外部标识符）
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Detail(drogon::HttpRequestPtr request, std::string share_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 更新分享设置
         * @param request HTTP请求对象
         * @param share_id 分享码（外部标识符）
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Update(drogon::HttpRequestPtr request, std::string share_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 批量取消分享
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Cancel(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 验证分享访问
         * @param request HTTP请求对象
         * @param share_id 分享码（外部标识符）
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Access(drogon::HttpRequestPtr request, std::string share_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 浏览分享内容
         * @param request HTTP请求对象
         * @param share_id 分享码（外部标识符）
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Browse(drogon::HttpRequestPtr request, std::string share_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 下载分享文件
         * @param request HTTP请求对象
         * @param share_id 分享码（外部标识符）
         * @param file_id 文件ID
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Download(drogon::HttpRequestPtr request, std::string share_id, std::string file_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<ShareService> m_share_service;
    };

} // namespace disk::share
