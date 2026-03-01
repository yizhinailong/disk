/**
 * @file AuthController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证控制器
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AuthController.hpp"

#include "utils/Response.hpp"

namespace disk::auth {
    AuthController::AuthController()
        : m_auth_service(std::make_unique<AuthService>(drogon::app().getRedisClient())) {
    }

    auto AuthController::Register(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received user registration request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = RegisterRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "User registration request validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "User registration parameters validated: " << parse_result->username;

        // 2. 调用 Service 层注册用户
        auto register_result = co_await m_auth_service->Register(*parse_result);
        if (!register_result) {
            LOG_ERROR << "User registration business logic failed: "
                      << register_result.error().message << " (username: " << parse_result->username
                      << ")";
            co_return Response::Error(register_result.error());
        }

        // 3. 构造响应
        Json::Value data;
        data["user"] = register_result->ToJson();

        LOG_INFO << "User registration successful: " << register_result->username
                 << " (ID: " << register_result->id << ")";
        co_return Response::Success(data);
    }

    auto AuthController::Login(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received login request: " << request->getPeerAddr().toIpPort();

        // 1. 解析请求
        auto parse_result = LoginRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Login request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 2. 调用 Service 登录
        const auto ip_address = request->getPeerAddr().toIpPort();
        auto login_result = co_await m_auth_service->Login(*parse_result, ip_address);

        if (!login_result) {
            LOG_ERROR << "Login failed: " << login_result.error().message;
            co_return Response::Error(login_result.error());
        }

        // 3. 构造响应
        Json::Value data;
        data["access_token"] = login_result->access_token;
        data["refresh_token"] = login_result->refresh_token;
        data["token_type"] = login_result->token_type;
        data["expires_in"] = login_result->expires_in;
        data["user"] = login_result->user.ToJson();

        LOG_INFO << "Login successful: " << parse_result->account;
        co_return Response::Success(data);
    }

    auto AuthController::RefreshTokens(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received refresh token request: " << request->getPeerAddr().toIpPort();

        // 1. 解析请求
        auto parse_result = RefreshTokenRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Refresh token request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 2. 调用 Service 刷新令牌
        auto refresh_result = co_await m_auth_service->RefreshTokens(*parse_result);

        if (!refresh_result) {
            LOG_ERROR << "Refresh token failed: " << refresh_result.error().message;
            co_return Response::Error(refresh_result.error());
        }

        // 3. 构造响应
        Json::Value data;
        data["access_token"] = refresh_result->access_token;
        data["refresh_token"] = refresh_result->refresh_token;
        data["expires_in"] = refresh_result->expires_in;

        LOG_INFO << "Refresh token successful";
        co_return Response::Success(data);
    }

    auto AuthController::Logout(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received logout request: " << request->getPeerAddr().toIpPort();

        // 步骤 1: 提取 access_token 从 Authorization header
        const auto& auth_header = request->getHeader("Authorization");
        if (auth_header.empty()) {
            LOG_WARN << "Logout request missing Authorization header";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMissing));
        }

        if (!auth_header.starts_with("Bearer ")) {
            LOG_WARN << "Logout request Authorization header format invalid";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMalformed));
        }

        const auto access_token = auth_header.substr(7);

        // 步骤 2: 提取 user_id 从请求属性（由 JwtAuthFilter 设置）
        if (!request->attributes()->find("user_id")) {
            LOG_WARN << "Logout request missing user_id attribute";
            co_return Response::Error(ErrorInfo(ErrorCode::InvalidToken));
        }
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 步骤 3: 提取 IP 地址
        const auto ip_address = request->getPeerAddr().toIpPort();

        // 步骤 4: 调用 Service 层登出
        auto logout_result = co_await m_auth_service->Logout(user_id, access_token, ip_address);
        if (!logout_result) {
            LOG_ERROR << "Logout failed: " << logout_result.error().message;
            co_return Response::Error(logout_result.error());
        }

        // 步骤 5: 返回成功响应
        LOG_INFO << "Logout successful: user_id=" << user_id;
        co_return Response::Success({});
    }
} // namespace disk::auth
