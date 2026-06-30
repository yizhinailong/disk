/**
 * @file AdminController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员用户管理控制器
 * @note Request 和 Response DTO 定义在 dtos/AdminDto.hpp
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/AdminService.hpp"

namespace disk::controllers {

    /**
     * @brief 管理员用户管理控制器
     *
     * @details
     * 路由：
     * - GET    /api/admin/users           获取用户列表
     * - GET    /api/admin/users/{id}       获取用户详情
     * - PUT    /api/admin/users/{id}/status 修改用户状态
     * - PUT    /api/admin/users/{id}/role  修改用户角色
     * - DELETE /api/admin/users/{id}       软删除用户
     * - GET    /api/admin/storage/stats    获取全局存储统计
     * - GET    /api/admin/shares            获取分享列表
     * - GET    /api/admin/shares/{id}       获取分享详情
     * - DELETE /api/admin/shares/{id}       强制取消分享
     * - GET    /api/admin/stats/overview    获取系统概览统计
     * - GET    /api/admin/stats/system      获取系统状态
     *
     * 所有接口需要 JWT 认证 + 管理员权限
     */
    class AdminController : public drogon::HttpController<AdminController> {
    public:
        AdminController() = default;

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            AdminController::ListUsers,
            "/api/admin/users",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::GetUserDetail,
            "/api/admin/users/{id}",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::ChangeUserStatus,
            "/api/admin/users/{id}/status",
            drogon::Put,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::ChangeUserRole,
            "/api/admin/users/{id}/role",
            drogon::Put,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::ChangeUserAvailableSpace,
            "/api/admin/users/{id}/available-space",
            drogon::Put,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::SoftDeleteUser,
            "/api/admin/users/{id}",
            drogon::Delete,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::GetGlobalStorageStats,
            "/api/admin/storage/stats",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::ListShares,
            "/api/admin/shares",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::GetShareDetail,
            "/api/admin/shares/{id}",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::ForceCancelShare,
            "/api/admin/shares/{id}",
            drogon::Delete,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::GetOverviewStats,
            "/api/admin/stats/overview",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::GetSystemStatus,
            "/api/admin/stats/system",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        ADD_METHOD_TO(
            AdminController::GetAdminLogs,
            "/api/admin/logs",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter",
        );
        METHOD_LIST_END

        [[nodiscard]]
        auto ListUsers(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto GetUserDetail(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto ChangeUserStatus(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto ChangeUserRole(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto ChangeUserAvailableSpace(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto SoftDeleteUser(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto GetGlobalStorageStats(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto ListShares(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto GetShareDetail(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto ForceCancelShare(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto GetOverviewStats(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto GetSystemStatus(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto GetAdminLogs(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;
    };

} ///< namespace disk::controllers
