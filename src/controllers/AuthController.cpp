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
        LOG_DEBUG << "AuthController 初始化完成";
    }

    auto AuthController::Register(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到用户注册请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = RegisterRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "用户注册请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "用户注册参数验证通过: " << parse_result->username;

        // 2. 调用 Service 层注册用户
        auto register_result = co_await m_auth_service->Register(*parse_result);
        if (!register_result) {
            LOG_ERROR << "用户注册业务逻辑失败: " << register_result.error().message
                      << " (用户名: " << parse_result->username << ")";
            co_return Response::Error(register_result.error());
        }

        // 3. 构造响应
        Json::Value data;
        data["user"] = register_result->ToJson();

        LOG_INFO << "用户注册成功: " << register_result->username << " (ID: " << register_result->id << ")";
        co_return Response::Success(data);
    }

    auto AuthController::Login(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到登录请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析请求
        auto parse_result = LoginRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "登录请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 2. 调用 Service 登录
        const auto ip_address = request->getPeerAddr().toIpPort();
        auto login_result = co_await m_auth_service->Login(*parse_result, ip_address);

        if (!login_result) {
            LOG_ERROR << "登录失败: " << login_result.error().message;
            co_return Response::Error(login_result.error());
        }

        // 3. 构造响应
        Json::Value data;
        data["access_token"] = login_result->access_token;
        data["refresh_token"] = login_result->refresh_token;
        data["token_type"] = login_result->token_type;
        data["expires_in"] = login_result->expires_in;
        data["user"] = login_result->user.ToJson();

        LOG_INFO << "登录成功: " << parse_result->account;
        co_return Response::Success(data);
    }

    auto AuthController::RefreshTokens(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到刷新令牌请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析请求
        auto parse_result = RefreshTokenRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "刷新令牌请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 2. 调用 Service 刷新令牌
        auto refresh_result = co_await m_auth_service->RefreshTokens(*parse_result);

        if (!refresh_result) {
            LOG_ERROR << "刷新令牌失败: " << refresh_result.error().message;
            co_return Response::Error(refresh_result.error());
        }

        // 3. 构造响应
        Json::Value data;
        data["access_token"] = refresh_result->access_token;
        data["refresh_token"] = refresh_result->refresh_token;
        data["expires_in"] = refresh_result->expires_in;

        LOG_INFO << "刷新令牌成功";
        co_return Response::Success(data);
    }

    auto AuthController::Logout(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到登出请求: " << request->getPeerAddr().toIpPort();

        // 步骤 1: 提取 access_token 从 Authorization header
        const auto& auth_header = request->getHeader("Authorization");
        if (auth_header.empty()) {
            LOG_WARN << "登出请求缺少 Authorization header";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMissing));
        }

        if (!auth_header.starts_with("Bearer ")) {
            LOG_WARN << "登出请求 Authorization header 格式错误";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMalformed));
        }

        const auto access_token = auth_header.substr(7);

        // 步骤 2: 提取 user_id 从请求属性（由 JwtAuthFilter 设置）
        if (!request->attributes()->find("user_id")) {
            LOG_WARN << "登出请求缺少 user_id attribute";
            co_return Response::Error(ErrorInfo(ErrorCode::InvalidToken));
        }
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 步骤 3: 提取 IP 地址
        const auto ip_address = request->getPeerAddr().toIpPort();

        // 步骤 4: 调用 Service 层登出
        auto logout_result = co_await m_auth_service->Logout(user_id, access_token, ip_address);
        if (!logout_result) {
            LOG_ERROR << "登出失败: " << logout_result.error().message;
            co_return Response::Error(logout_result.error());
        }

        // 步骤 5: 返回成功响应
        LOG_INFO << "登出成功: user_id=" << user_id;
        co_return Response::Success({});
    }
} // namespace disk::auth
