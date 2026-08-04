/**
 * @file AuthController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AuthController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "utils/ClientIp.hpp"
#include "utils/Response.hpp"

namespace disk::auth {
    AuthController::AuthController()
        : m_auth_service(std::make_unique<AuthService>(drogon::app().getRedisClient())) {
    }

    auto AuthController::Register(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "auth");

        Logger::Info(log_context) << "Received user registration request";

        /// 1. 解析并验证请求参数
        auto parse_result = RegisterRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "User registration request validation failed";
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context) << "User registration parameters validated";

        /// 2. 调用 Service 层注册用户
        auto register_result = co_await m_auth_service->Register(*parse_result, log_context);
        if (!register_result) {
            Logger::Error(log_context) << "User registration business logic failed";
            co_return Response::Error(register_result.error());
        }

        /// 3. 构造响应
        Json::Value data;
        data["user"] = register_result->ToJson();

        Logger::Info(log_context) << "User registration successful";
        co_return Response::Success(data);
    }

    auto AuthController::Login(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "auth");

        Logger::Info(log_context) << "Received login request";

        /// 1. 解析请求
        auto parse_result = LoginRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "Login request validation failed";
            co_return Response::Error(parse_result.error());
        }

        /// 2. 调用 Service 登录
        const auto ip_address = disk::utils::ResolveClientIp(request);
        auto login_result =
            co_await m_auth_service->Login(*parse_result, ip_address, log_context);

        if (!login_result) {
            Logger::Error(log_context) << "Login failed";
            co_return Response::Error(login_result.error());
        }

        /// 3. 构造响应
        Logger::Info(log_context) << "Login successful";
        co_return Response::Success(login_result->ToJson());
    }

    auto AuthController::RefreshTokens(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "auth");

        Logger::Info(log_context) << "Received refresh token request";

        /// 1. 解析请求
        auto parse_result = RefreshTokenRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "Refresh token request validation failed";
            co_return Response::Error(parse_result.error());
        }

        /// 2. 调用 Service 刷新令牌
        auto refresh_result =
            co_await m_auth_service->RefreshTokens(*parse_result, log_context);

        if (!refresh_result) {
            Logger::Error(log_context) << "Refresh token failed";
            co_return Response::Error(refresh_result.error());
        }

        /// 3. 构造响应
        Logger::Info(log_context) << "Refresh token successful";
        co_return Response::Success(refresh_result->ToJson());
    }

    auto AuthController::Logout(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "auth");

        Logger::Info(log_context)
            << "Received logout request: " << request->getPeerAddr().toIpPort();

        /// 步骤 1: 提取 access_token 从 Authorization header
        const auto& auth_header = request->getHeader("Authorization");
        if (auth_header.empty()) {
            Logger::Warn(log_context) << "Logout request missing Authorization header";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMissing));
        }

        if (!auth_header.starts_with("Bearer ")) {
            Logger::Warn(log_context)
                << "Logout request Authorization header format invalid";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMalformed));
        }

        const auto access_token = auth_header.substr(7);

        /// 步骤 2: 提取 user_id 从请求属性（由 JwtAuthFilter 设置）
        if (!request->attributes()->find("user_id")) {
            Logger::Warn(log_context) << "Logout request missing user_id attribute";
            co_return Response::Error(ErrorInfo(ErrorCode::InvalidToken));
        }
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 步骤 3: 提取 IP 地址
        const auto ip_address = disk::utils::ResolveClientIp(request);

        /// 步骤 4: 调用 Service 层登出
        auto logout_result =
            co_await m_auth_service->Logout(user_id, access_token, ip_address, log_context);
        if (!logout_result) {
            Logger::Error(log_context) << "Logout failed: " << logout_result.error().message;
            co_return Response::Error(logout_result.error());
        }

        /// 步骤 5: 返回成功响应
        Logger::Info(log_context) << "Logout successful: user_id=" << user_id;
        co_return Response::Success({});
    }
} // namespace disk::auth
