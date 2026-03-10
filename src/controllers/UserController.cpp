/**
 * @file UserController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户控制器实现
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
    }

    auto UserController::GetProfile(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received user info request: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Call service
        auto profile_result = co_await m_user_service->GetProfile(user_id);

        // Step 3: Handle service errors
        if (!profile_result) {
            LOG_ERROR << "Failed to get user info: " << profile_result.error().message;
            co_return Response::Error(profile_result.error());
        }

        // Step 4: Wrap successful response
        Json::Value data;
        data["user"] = profile_result->ToJson();

        LOG_INFO << "User info retrieved successfully: user_id=" << user_id;
        co_return Response::Success(data);
    }

    auto UserController::UpdatePassword(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received change password request: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Parse and validate request DTO
        auto parse_result = ChangePasswordRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Change password request validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // Step 3: Call service
        auto change_result = co_await m_user_service->ChangePassword(user_id, *parse_result);

        // Step 4: Handle service errors
        if (!change_result) {
            LOG_ERROR << "Failed to change password: " << change_result.error().message;
            co_return Response::Error(change_result.error());
        }

        // Step 5: Return success (data: null for PUT password)
        LOG_INFO << "Change password successful: user_id=" << user_id;
        co_return Response::Success();
    }

    auto UserController::UpdateProfile(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received profile update request: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Parse and validate request DTO
        auto parse_result = UpdateProfileRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Profile update request validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // Step 3: Call service
        auto update_result = co_await m_user_service->UpdateProfile(user_id, *parse_result);

        // Step 4: Handle service errors
        if (!update_result) {
            LOG_ERROR << "Failed to update profile: " << update_result.error().message;
            co_return Response::Error(update_result.error());
        }

        // Step 5: Return updated profile
        Json::Value data;
        data["user"] = update_result->ToJson();

        LOG_INFO << "Profile update successful: user_id=" << user_id;
        co_return Response::Success(data);
    }

    auto UserController::GetStorage(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received storage stats request: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Call service
        auto result = co_await m_user_service->GetStorage(user_id);
        if (!result) {
            LOG_ERROR << "Failed to get storage stats: " << result.error().message;
            co_return Response::Error(result.error());
        }

        // Step 3: Return success response
        Json::Value data;
        data = result->ToJson();

        LOG_INFO << "Get storage stats successful: user_id=" << user_id;
        co_return Response::Success(data);
    }

} // namespace disk::user
