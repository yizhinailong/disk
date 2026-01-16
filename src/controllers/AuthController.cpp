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

#include "requests/AuthRequest.hpp"
#include "utils/Response.hpp"

namespace disk::auth {
    AuthController::AuthController()
        : m_auth_service(std::make_unique<AuthService>(drogon::app().getDbClient())) {}

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
} // namespace disk::auth
