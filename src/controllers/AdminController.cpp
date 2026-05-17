/**
 * @file AdminController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员用户管理控制器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AdminController.hpp"

#include "dtos/AdminDto.hpp"
#include "utils/Response.hpp"

namespace disk::controllers {

    auto AdminController::ListUsers(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin list users request: " << request->getPeerAddr().toIpPort();

        auto parse_result = admin::ListUsersRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "List users request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ListUsers(*parse_result);

        if (!result) {
            LOG_ERROR << "Failed to list users: " << result.error().message;
            co_return Response::Error(result.error());
        }

        LOG_INFO << "Admin list users successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetUserDetail(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin get user detail request: " << request->getPeerAddr().toIpPort();

        auto id_str = request->getParameter("id");
        if (id_str.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t user_id = 0;
        try {
            user_id = std::stoull(id_str);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetUserDetail(user_id);

        if (!result) {
            LOG_ERROR << "Failed to get user detail: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["user"] = result->ToJson();

        LOG_INFO << "Admin get user detail successful: user_id=" << user_id;
        co_return Response::Success(data);
    }

    auto AdminController::ChangeUserStatus(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin change user status request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        auto id_str = request->getParameter("id");
        if (id_str.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t target_id = 0;
        try {
            target_id = std::stoull(id_str);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto parse_result = admin::ChangeStatusRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Change status request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserStatus(
            target_id, parse_result->status, operator_id
        );

        if (!result) {
            LOG_ERROR << "Failed to change user status: " << result.error().message;
            co_return Response::Error(result.error());
        }

        LOG_INFO << "Admin change user status successful: target_id=" << target_id;
        co_return Response::Success();
    }

    auto AdminController::ChangeUserRole(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin change user role request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        auto id_str = request->getParameter("id");
        if (id_str.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t target_id = 0;
        try {
            target_id = std::stoull(id_str);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto parse_result = admin::ChangeRoleRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Change role request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserRole(
            target_id, parse_result->role, operator_id
        );

        if (!result) {
            LOG_ERROR << "Failed to change user role: " << result.error().message;
            co_return Response::Error(result.error());
        }

        LOG_INFO << "Admin change user role successful: target_id=" << target_id;
        co_return Response::Success();
    }

    auto AdminController::SoftDeleteUser(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin soft delete user request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        auto id_str = request->getParameter("id");
        if (id_str.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t target_id = 0;
        try {
            target_id = std::stoull(id_str);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->SoftDeleteUser(target_id, operator_id);

        if (!result) {
            LOG_ERROR << "Failed to soft delete user: " << result.error().message;
            co_return Response::Error(result.error());
        }

        LOG_INFO << "Admin soft delete user successful: target_id=" << target_id;
        co_return Response::Success();
    }

    auto AdminController::ListUserFiles(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin list user files request: " << request->getPeerAddr().toIpPort();

        auto id_str = request->getParameter("id");
        if (id_str.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t user_id = 0;
        try {
            user_id = std::stoull(id_str);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto parse_result = admin::ListUserFilesRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "List user files request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ListUserFiles(user_id, *parse_result);

        if (!result) {
            LOG_ERROR << "Failed to list user files: " << result.error().message;
            co_return Response::Error(result.error());
        }

        LOG_INFO << "Admin list user files successful: user_id=" << user_id;
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetUserStorage(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin get user storage request: " << request->getPeerAddr().toIpPort();

        auto id_str = request->getParameter("id");
        if (id_str.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t user_id = 0;
        try {
            user_id = std::stoull(id_str);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetUserStorage(user_id);

        if (!result) {
            LOG_ERROR << "Failed to get user storage: " << result.error().message;
            co_return Response::Error(result.error());
        }

        LOG_INFO << "Admin get user storage successful: user_id=" << user_id;
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetGlobalStorageStats(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Admin get global storage stats request: " << request->getPeerAddr().toIpPort();

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetGlobalStorageStats();

        if (!result) {
            LOG_ERROR << "Failed to get global storage stats: " << result.error().message;
            co_return Response::Error(result.error());
        }

        LOG_INFO << "Admin get global storage stats successful";
        co_return Response::Success(result->ToJson());
    }

} // namespace disk::controllers
