/**
 * @file UserController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户控制器
 * @note Request 和 Response DTO 定义在 dtos/UserDto.hpp
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/UserService.hpp"

namespace disk::user {

    // ==================== Controller ====================

    /**
     * @brief 用户控制器
     *
     * 业务规则：
     * - 所有接口需要 JWT 认证（通过 JwtAuthFilter 自动处理）
     * - 用户信息从请求 attributes 中的 user_id 获取（由 JwtAuthFilter 设置）
     * - GetProfile: 获取用户基本信息（用户名、邮箱、存储配额等）
     * - UpdateProfile: 更新用户资料（昵称、头像等）
     * - UpdatePassword: 修改用户密码
     * - GetStorage: 获取用户存储空间使用统计
     */
    class UserController : public drogon::HttpController<UserController> {
    public:
        UserController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            UserController::GetProfile,
            "/api/user/profile",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            UserController::UpdateProfile,
            "/api/user/profile",
            drogon::Patch,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            UserController::UpdatePassword,
            "/api/user/password",
            drogon::Put,
            "disk::filters::JwtAuthFilter",
        );
        ADD_METHOD_TO(
            UserController::GetStorage,
            "/api/user/storage",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
        );
        METHOD_LIST_END

        /**
         * @brief 获取用户信息
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 验证 user_id 存在（返回 TokenMissing 如果不存在）
         * - 调用 UserService::GetProfile(user_id) 获取用户信息
         * - 处理服务层错误（返回 Response::Error）
         * - 将成功响应包装在 Json::Value 中，使用 data.user 格式
         * - 返回 HTTP 200 状态码和 Response::Success(data)
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto GetProfile(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 修改密码
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 解析请求 JSON 提取 old_password 和 new_password
         * - 调用 UserService::ChangePassword(user_id, request) 修改密码
         * - 处理服务层错误（返回 Response::Error）
         * - 成功时返回 HTTP 200 状态码和 Response::Success({})（data 为 null）
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto UpdatePassword(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 更新用户资料
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 解析请求 JSON 通过 UpdateProfileRequest 提取 nickname 和 avatar
         * - 调用 UserService::UpdateProfile(user_id, request) 更新用户资料
         * - 处理服务层错误（返回 Response::Error）
         * - 成功时返回 HTTP 200 状态码和 Response::Success(data)
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto UpdateProfile(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 获取存储空间统计
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 调用 UserService::GetStorage(user_id) 获取存储统计
         * - 处理服务层错误（返回 Response::Error）
         * - 返回 HTTP 200 状态码和 Response::Success(data)
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto GetStorage(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<disk::user::UserService> m_user_service;
    };

} // namespace disk::user
