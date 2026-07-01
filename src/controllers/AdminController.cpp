/**
 * @file AdminController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员用户管理控制器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AdminController.hpp"

#include "application/ApplicationContext.hpp"
#include "dtos/AdminDto.hpp"
#include "utils/Response.hpp"

namespace disk::controllers {

    auto AdminController::ListUsers(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin list users request: " << request->getPeerAddr().toIpPort();

        auto parse_result = admin::ListUsersRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "List users request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ListUsers(*parse_result);

        if (!result) {
            Logger::Error() << "Failed to list users: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin list users successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetUserDetail(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin get user detail request: " << request->getPeerAddr().toIpPort();

        if (id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t user_id = 0;
        try {
            user_id = std::stoull(id);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetUserDetail(user_id);

        if (!result) {
            Logger::Error() << "Failed to get user detail: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["user"] = result->ToJson();

        Logger::Info() << "Admin get user detail successful: user_id=" << user_id;
        co_return Response::Success(data);
    }

    auto AdminController::ChangeUserStatus(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin change user status request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        if (id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t target_id = 0;
        try {
            target_id = std::stoull(id);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto parse_result = admin::ChangeStatusRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Change status request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserStatus(
            target_id, parse_result->status, operator_id
        );

        if (!result) {
            Logger::Error() << "Failed to change user status: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin change user status successful: target_id=" << target_id;
        co_return Response::Success();
    }

    auto AdminController::ChangeUserRole(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin change user role request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        if (id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t target_id = 0;
        try {
            target_id = std::stoull(id);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto parse_result = admin::ChangeRoleRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Change role request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserRole(
            target_id, parse_result->role, operator_id
        );

        if (!result) {
            Logger::Error() << "Failed to change user role: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin change user role successful: target_id=" << target_id;
        co_return Response::Success();
    }

    auto AdminController::ChangeUserAvailableSpace(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin change user available space request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        if (id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t target_id = 0;
        try {
            target_id = std::stoull(id);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto parse_result = admin::ChangeAvailableSpaceRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Change available space request validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserAvailableSpace(
            target_id, parse_result->available_space_g, operator_id
        );

        if (!result) {
            Logger::Error() << "Failed to change user available space: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["user"] = result->ToJson();

        Logger::Info() << "Admin change user available space successful: target_id=" << target_id;
        co_return Response::Success(data);
    }


    auto AdminController::SoftDeleteUser(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin soft delete user request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        if (id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t target_id = 0;
        try {
            target_id = std::stoull(id);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid user id format"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->SoftDeleteUser(target_id, operator_id);

        if (!result) {
            Logger::Error() << "Failed to soft delete user: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin soft delete user successful: target_id=" << target_id;
        co_return Response::Success();
    }

    auto AdminController::GetGlobalStorageStats(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin get global storage stats request: " << request->getPeerAddr().toIpPort();

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetGlobalStorageStats();

        if (!result) {
            Logger::Error() << "Failed to get global storage stats: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin get global storage stats successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::ListShares(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin list shares request: " << request->getPeerAddr().toIpPort();

        auto parse_result = admin::ListSharesRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "List shares request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ListShares(*parse_result);

        if (!result) {
            Logger::Error() << "Failed to list shares: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin list shares successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetShareDetail(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin get share detail request: " << request->getPeerAddr().toIpPort();

        if (id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t share_id = 0;
        try {
            share_id = std::stoull(id);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid share id format"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetShareDetail(share_id);

        if (!result) {
            Logger::Error() << "Failed to get share detail: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["share"] = result->ToJson();

        Logger::Info() << "Admin get share detail successful: share_id=" << share_id;
        co_return Response::Success(data);
    }

    auto AdminController::ForceCancelShare(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin force cancel share request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        if (id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: id"
            ));
        }

        uint64_t share_id = 0;
        try {
            share_id = std::stoull(id);
        } catch (const std::exception&) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Invalid share id format"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ForceCancelShare(share_id, operator_id);

        if (!result) {
            Logger::Error() << "Failed to force cancel share: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin force cancel share successful: share_id=" << share_id;
        co_return Response::Success();
    }

    auto AdminController::GetOverviewStats(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin get overview stats request: " << request->getPeerAddr().toIpPort();

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetOverviewStats();

        if (!result) {
            Logger::Error() << "Failed to get overview stats: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin get overview stats successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetSystemStatus(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin get system status request: " << request->getPeerAddr().toIpPort();

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetSystemStatus();

        if (!result) {
            Logger::Error() << "Failed to get system status: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin get system status successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetAdminLogs(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin list logs request: " << request->getPeerAddr().toIpPort();

        auto parse_result = admin::AdminLogListRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "List logs request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetAdminLogs(*parse_result);

        if (!result) {
            Logger::Error() << "Failed to list logs: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Admin list logs successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::RunExpiredCleanup(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Admin run expired cleanup request: " << request->getPeerAddr().toIpPort();

        auto& cleanup_service = application::ApplicationContext::GetInstance()->Cleanup();
        auto result = co_await cleanup_service.RunExpiredCleanupOnce();
        if (!result) {
            Logger::Error() << "Failed to run expired cleanup: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["expired_trash_deleted"] = result->expired_trash_deleted;
        data["expired_upload_tasks_cleaned"] = result->expired_upload_tasks_cleaned;

        Logger::Info() << "Admin run expired cleanup successful: expired_trash_deleted="
                 << result->expired_trash_deleted
                 << ", expired_upload_tasks_cleaned=" << result->expired_upload_tasks_cleaned;
        co_return Response::Success(data);
    }

} ///< namespace disk::controllers
