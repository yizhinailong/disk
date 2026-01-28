/**
 * @file UserController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户控制器实现
 * @version 0.1
 * @date 2026-01-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UserController.hpp"

#include "utils/Response.hpp"

namespace disk::user {

    UserController::UserController()
        : m_user_service(std::make_unique<UserService>(drogon::app().getDbClient())) {
        LOG_DEBUG << "UserController 初始化完成";
    }

    auto UserController::GetProfile(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到用户信息请求: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Call service
        auto profile_result = co_await m_user_service->GetProfile(user_id);

        // Step 3: Handle service errors
        if (!profile_result) {
            LOG_ERROR << "获取用户信息失败: " << profile_result.error().message;
            co_return Response::Error(profile_result.error());
        }

        // Step 4: Wrap successful response
        Json::Value data;
        data["user"] = profile_result->ToJson();

        LOG_INFO << "用户信息获取成功: user_id=" << user_id;
        co_return Response::Success(data);
    }

} // namespace disk::user
