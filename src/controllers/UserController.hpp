/**
 * @file UserController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户控制器
 * @note Request 和 Response DTO 定义在 dtos/UserDto.hpp
 * @version 0.1
 * @date 2026-01-28
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
     * - UpdateProfile: 更新用户基本信息（用户名、邮箱等）
     * - UpdatePassword: 修改用户密码
     * - GetStorage: 获取用户存储空间使用统计
     */
    class UserController : public drogon::HttpController<UserController> {
    public:
        UserController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(UserController::GetProfile, "/api/user/profile", drogon::Get);
        ADD_METHOD_TO(UserController::UpdatePassword, "/api/user/password", drogon::Put);
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
        auto UpdatePassword(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<disk::user::UserService> m_user_service;
    };

} // namespace disk::user
