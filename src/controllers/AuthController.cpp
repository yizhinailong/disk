#include "AuthController.hpp"

#include "requests/AuthRequest.hpp"
#include "utils/Response.hpp"

namespace disk::auth {
    auto AuthController::Register(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        // 解析并验证请求参数
        auto result = RegisterRequest::FromRequest(request);
        if (!result) {
            co_return Response::Error(result.error());
        }

        LOG_DEBUG << "name = " << result->username
                  << ", email = " << result->email
                  << ", password = " << result->password;

        // TODO: 调用 Service 层注册用户

        // 构造返回数据
        Json::Value data;
        data["username"] = result->username;

        co_return Response::Success(data);
    }
} // namespace disk::auth
