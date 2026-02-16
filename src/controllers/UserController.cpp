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

#include "dtos/UserDto.hpp"
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

    auto UserController::UpdatePassword(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到修改密码请求: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Parse and validate request DTO
        auto parse_result = ChangePasswordRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "修改密码请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // Step 3: Call service
        auto change_result = co_await m_user_service->ChangePassword(user_id, *parse_result);

        // Step 4: Handle service errors
        if (!change_result) {
            LOG_ERROR << "修改密码失败: " << change_result.error().message;
            co_return Response::Error(change_result.error());
        }

        // Step 5: Return success (data: null for PUT password)
        LOG_INFO << "修改密码成功: user_id=" << user_id;
        co_return Response::Success();
    }

    auto UserController::UpdateProfile(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到用户资料更新请求: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Parse and validate request DTO
        auto parse_result = UpdateProfileRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "用户资料更新请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // Step 3: Call service
        auto update_result = co_await m_user_service->UpdateProfile(user_id, *parse_result);

        // Step 4: Handle service errors
        if (!update_result) {
            LOG_ERROR << "用户资料更新失败: " << update_result.error().message;
            co_return Response::Error(update_result.error());
        }

        // Step 5: Return updated profile
        Json::Value data;
        data["user"] = update_result->ToJson();

        LOG_INFO << "用户资料更新成功: user_id=" << user_id;
        co_return Response::Success(data);
    }

    auto UserController::GetStorage(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到获取存储统计请求: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Call service
        auto result = co_await m_user_service->GetStorage(user_id);
        if (!result) {
            LOG_ERROR << "获取存储统计失败: " << result.error().message;
            co_return Response::Error(result.error());
        }

        // Step 3: Return success response
        Json::Value data;
        data = result->ToJson();

        LOG_INFO << "获取存储统计成功: user_id=" << user_id;
        co_return Response::Success(data);
    }

} // namespace disk::user
