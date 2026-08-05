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
#include "controllers/ControllerHelpers.hpp"
#include "dtos/AdminDto.hpp"
#include "utils/Response.hpp"

namespace disk::controllers {

    auto AdminController::ListUsers(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Admin list users request";

        auto parse_result = admin::ListUsersRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "List users request validation failed";
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ListUsers(*parse_result, log_context);

        if (!result) {
            Logger::Error(log_context) << "Failed to list users";
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin list users successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetUserDetail(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Admin get user detail request";

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
        auto result = co_await service->GetUserDetail(user_id, log_context);

        if (!result) {
            Logger::Error(log_context) << "Failed to get user detail";
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["user"] = result->ToJson();

        Logger::Info(log_context) << "Admin get user detail successful";
        co_return Response::Success(data);
    }

    auto AdminController::ChangeUserStatus(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Admin change user status request";

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

        auto parse_result = admin::ChangeStatusRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "Change status request validation failed";
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserStatus(
            target_id,
            parse_result->status,
            operator_id,
            log_context
        );

        if (!result) {
            Logger::Error(log_context) << "Failed to change user status";
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin change user status successful";
        co_return Response::Success();
    }

    auto AdminController::ChangeUserRole(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Admin change user role request";

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

        auto parse_result = admin::ChangeRoleRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "Change role request validation failed";
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserRole(
            target_id,
            parse_result->role,
            operator_id,
            log_context
        );

        if (!result) {
            Logger::Error(log_context) << "Failed to change user role";
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin change user role successful";
        co_return Response::Success();
    }

    auto AdminController::ChangeUserAvailableSpace(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Admin change user available space request";

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

        auto parse_result =
            admin::ChangeAvailableSpaceRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "Change available space request validation failed";
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->ChangeUserAvailableSpace(
            target_id,
            parse_result->available_space_g,
            operator_id,
            log_context
        );

        if (!result) {
            Logger::Error(log_context) << "Failed to change user available space";
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["user"] = result->ToJson();

        Logger::Info(log_context) << "Admin change user available space successful";
        co_return Response::Success(data);
    }

    auto AdminController::SoftDeleteUser(drogon::HttpRequestPtr request, std::string id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin soft delete user request: " << request->getPeerAddr().toIpPort();

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
        auto result =
            co_await service->SoftDeleteUser(target_id, operator_id, log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to soft delete user: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context)
            << "Admin soft delete user successful: target_id=" << target_id;
        co_return Response::Success();
    }

    auto AdminController::GetGlobalStorageStats(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin get global storage stats request: "
            << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");
        auto service = services::AdminService::GetInstance();
        auto result =
            co_await service->GetGlobalStorageStats(operator_id, log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to get global storage stats: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin get global storage stats successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::ListShares(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin list shares request: " << request->getPeerAddr().toIpPort();

        auto parse_result = admin::ListSharesRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "List shares request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto operator_id = request->attributes()->get<uint64_t>("user_id");
        auto service = services::AdminService::GetInstance();
        auto result =
            co_await service->ListShares(*parse_result, operator_id, log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to list shares: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin list shares successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetShareDetail(
        drogon::HttpRequestPtr request,
        std::string share_id
    )
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin get share detail request: " << request->getPeerAddr().toIpPort();

        if (share_id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: share_id"
            ));
        }

        auto operator_id = request->attributes()->get<uint64_t>("user_id");
        auto service = services::AdminService::GetInstance();
        auto result =
            co_await service->GetShareDetail(share_id, operator_id, log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to get share detail: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context)
            << "Admin get share detail successful: share_id=" << share_id;
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::ForceCancelShare(
        drogon::HttpRequestPtr request,
        std::string share_id
    )
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin force cancel share request: " << request->getPeerAddr().toIpPort();

        auto operator_id = request->attributes()->get<uint64_t>("user_id");

        if (share_id.empty()) {
            co_return Response::Error(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Missing required parameter: share_id"
            ));
        }

        auto service = services::AdminService::GetInstance();
        auto result =
            co_await service->ForceCancelShare(share_id, operator_id, log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to force cancel share: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context)
            << "Admin force cancel share successful: share_id=" << share_id;
        co_return Response::Success();
    }

    auto AdminController::GetOverviewStats(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin get overview stats request: " << request->getPeerAddr().toIpPort();

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetOverviewStats(log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to get overview stats: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin get overview stats successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetSystemStatus(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin get system status request: " << request->getPeerAddr().toIpPort();

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetSystemStatus(log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to get system status: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin get system status successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::GetAdminLogs(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context)
            << "Admin list logs request: " << request->getPeerAddr().toIpPort();

        auto parse_result = admin::AdminLogListRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "List logs request validation failed: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        auto service = services::AdminService::GetInstance();
        auto result = co_await service->GetAdminLogs(*parse_result, log_context);

        if (!result) {
            Logger::Error(log_context)
                << "Failed to list logs: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Admin list logs successful";
        co_return Response::Success(result->ToJson());
    }

    auto AdminController::RunExpiredCleanup(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto log_context = GetRequestLogContext(request, "cleanup");
        Logger::Info(log_context)
            << "Admin run expired cleanup request: " << request->getPeerAddr().toIpPort();

        auto& cleanup_service = application::ApplicationContext::GetInstance()->Cleanup();
        auto result = co_await cleanup_service.RunExpiredCleanupOnce(log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Failed to run expired cleanup: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Json::Value data;
        data["expired_trash_deleted"] = result->expired_trash_deleted;
        data["expired_upload_tasks_cleaned"] = result->expired_upload_tasks_cleaned;

        Logger::Info(log_context)
            << "Admin run expired cleanup successful: expired_trash_deleted="
            << result->expired_trash_deleted
            << ", expired_upload_tasks_cleaned=" << result->expired_upload_tasks_cleaned;
        co_return Response::Success(data);
    }

} // namespace disk::controllers
