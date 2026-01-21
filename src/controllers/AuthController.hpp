/**
 * @file AuthController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证控制器
 * @note Request 和 Response DTO 定义在 dtos/AuthDto.hpp
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/AuthService.hpp"

namespace disk::auth {

    // ==================== Controller ====================

    class AuthController : public drogon::HttpController<AuthController> {
    public:
        AuthController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(AuthController::Register, "/api/auth/register", drogon::Post);
        ADD_METHOD_TO(AuthController::Login, "/api/auth/login", drogon::Post);
        ADD_METHOD_TO(AuthController::RefreshTokens, "/api/auth/refresh", drogon::Post);
        ADD_METHOD_TO(AuthController::Logout, "/api/auth/logout", drogon::Post);
        METHOD_LIST_END

        /**
         * @brief 用户注册
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Register(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 用户登录
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Login(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 刷新令牌
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto RefreshTokens(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 用户登出
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Logout(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<AuthService> m_auth_service;
    };

} // namespace disk::auth
