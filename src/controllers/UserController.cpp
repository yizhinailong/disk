/**
 * @file UserController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户控制器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UserController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "dtos/UserDto.hpp"
#include "services/ObservedDbClient.hpp"
#include "utils/Response.hpp"

namespace disk::user {

    UserController::UserController()
        : m_user_service(std::make_unique<UserService>(disk::metrics::ObserveDbClient(drogon::app().getDbClient()))) {
    }

    auto UserController::GetProfile(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "user");

        Logger::Info(log_context) << "Received user info request";

        /// 步骤 1: 从请求属性中提取 user_id
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 步骤 2: 调用服务层
        auto profile_result = co_await m_user_service->GetProfile(user_id, log_context);

        /// 步骤 3: 处理服务层错误
        if (!profile_result) {
            Logger::Error(log_context) << "Failed to get user info";
            co_return Response::Error(profile_result.error());
        }

        /// 步骤 4: 包装成功响应
        Json::Value data;
        data["user"] = profile_result->ToJson();

        Logger::Info(log_context) << "User info retrieved successfully";
        co_return Response::Success(data);
    }

    auto UserController::UpdatePassword(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "user");

        Logger::Info(log_context)
            << "Received change password request: " << request->getPeerAddr().toIpPort();

        /// 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 步骤 2: 解析并验证请求 DTO
        auto parse_result = ChangePasswordRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Change password request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        /// 步骤 3: 调用服务层
        auto change_result =
            co_await m_user_service->ChangePassword(user_id, *parse_result, log_context);

        /// 步骤 4: 处理服务层错误
        if (!change_result) {
            Logger::Error(log_context)
                << "Failed to change password: " << change_result.error().message;
            co_return Response::Error(change_result.error());
        }

        /// 步骤 5: 返回成功响应（PUT 修改密码时 data 为 null）
        Logger::Info(log_context) << "Change password successful: user_id=" << user_id;
        co_return Response::Success();
    }

    auto UserController::UpdateProfile(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "user");

        Logger::Info(log_context)
            << "Received profile update request: " << request->getPeerAddr().toIpPort();

        /// 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 步骤 2: 解析并验证请求 DTO
        auto parse_result = UpdateProfileRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Profile update request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        /// 步骤 3: 调用服务层
        auto update_result =
            co_await m_user_service->UpdateProfile(user_id, *parse_result, log_context);

        /// 步骤 4: 处理服务层错误
        if (!update_result) {
            Logger::Error(log_context)
                << "Failed to update profile: " << update_result.error().message;
            co_return Response::Error(update_result.error());
        }

        /// 步骤 5: 返回更新后的用户资料
        Json::Value data;
        data["user"] = update_result->ToJson();

        Logger::Info(log_context) << "Profile update successful: user_id=" << user_id;
        co_return Response::Success(data);
    }

    auto UserController::GetStorage(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "user");

        Logger::Info(log_context)
            << "Received storage stats request: " << request->getPeerAddr().toIpPort();

        /// 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        /// 步骤 2: 调用服务层
        auto result = co_await m_user_service->GetStorage(user_id, log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Failed to get storage stats: " << result.error().message;
            co_return Response::Error(result.error());
        }

        /// 步骤 3: 返回成功响应
        Json::Value data;
        data = result->ToJson();

        Logger::Info(log_context) << "Get storage stats successful: user_id=" << user_id;
        co_return Response::Success(data);
    }

} // namespace disk::user
