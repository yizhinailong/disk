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
#include "services/AuthService.hpp"
#include "utils/Response.hpp"

namespace disk::auth {
    auto AuthController::Register(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        // 1. 解析并验证请求参数
        auto parse_result = RegisterRequest::FromRequest(request);
        if (!parse_result) {
            co_return Response::Error(parse_result.error());
        }

        // 2. 调用 Service 层注册用户
        auto db_client = drogon::app().getDbClient();
        AuthService service(db_client);
        auto register_result = co_await service.Register(*parse_result);

        // 3. 构造响应
        if (register_result) {
            Json::Value data;
            data["user"] = register_result->ToJson();
            co_return Response::Success(data);
        }

        co_return Response::Error(register_result.error());
    }
} // namespace disk::auth
