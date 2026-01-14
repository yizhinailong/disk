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
        // 1. 解析并验证请求参数
        auto parse_result = RegisterRequest::FromRequest(request);
        if (!parse_result) {
            co_return Response::Error(parse_result.error());
        }

        // 2. 调用 Service 层注册用户
        auto register_result = co_await m_auth_service->Register(*parse_result);
        if (!register_result) {
            co_return Response::Error(register_result.error());
        }

        // 3. 构造响应
        Json::Value data;
        data["user"] = register_result->ToJson();

        co_return Response::Success(data);
    }
} // namespace disk::auth
