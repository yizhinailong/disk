/**
 * @file AuthController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证控制器
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/AuthService.hpp"

namespace disk::auth {
    class AuthController : public drogon::HttpController<AuthController> {
    public:
        AuthController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(AuthController::Register, "/api/auth/register", drogon::Post);
        ADD_METHOD_TO(AuthController::Login, "/api/auth/login", drogon::Post);
        METHOD_LIST_END

        auto Register(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;
        auto Login(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<AuthService> m_auth_service;
    };
} // namespace disk::auth
