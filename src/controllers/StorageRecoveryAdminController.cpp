/**
 * @file StorageRecoveryAdminController.cpp
 * @brief Administrator HTTP boundary for audited storage recovery commands
 */

#include "controllers/StorageRecoveryAdminController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "dtos/StorageRecoveryAdminDto.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/StorageRecoveryAdminService.hpp"
#include "utils/Response.hpp"

namespace disk::controllers {
    namespace {
        [[nodiscard]] auto BuildService() -> disk::recovery::StorageRecoveryAdminService {
            return disk::recovery::StorageRecoveryAdminService(
                disk::metrics::ObserveDbClient(drogon::app().getDbClient())
            );
        }

        [[nodiscard]] auto BuildAuditContext(const drogon::HttpRequestPtr& request)
            -> disk::recovery::RecoveryAuditContext {
            return disk::recovery::RecoveryAuditContext{
                .operator_id = request->attributes()->get<uint64_t>("user_id"),
                .ip_address = request->getPeerAddr().toIp(),
                .user_agent = request->getHeader("User-Agent"),
            };
        }
    } // namespace

    auto StorageRecoveryAdminController::ReleaseUploadLease(
        drogon::HttpRequestPtr request,
        std::string upload_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Upload lease release request";

        auto parsed = disk::admin::UploadLeaseReleaseRequest::FromRequest(
            request,
            upload_id
        );
        if (!parsed) {
            Logger::Warn(log_context)
                << "Upload lease release validation failed: " << parsed.error().message;
            co_return Response::Error(parsed.error());
        }
        log_context.upload_id = parsed->upload_id;

        auto result = co_await BuildService().ReleaseUploadLease(
            parsed.value(),
            BuildAuditContext(request),
            log_context
        );
        if (!result) {
            Logger::Warn(log_context)
                << "Upload lease release failed: " << result.error().message;
            co_return Response::Error(result.error());
        }

        log_context.state_version = result->state_version;
        log_context.lease_owner = result->lease_owner;
        Logger::Info(log_context)
            << "Upload lease release successful: dry_run="
            << (result->dry_run ? "true" : "false");
        co_return Response::Success(result->ToJson());
    }

    auto StorageRecoveryAdminController::RebuildUploadCleanup(
        drogon::HttpRequestPtr request,
        std::string upload_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Upload cleanup rebuild request";

        auto parsed = disk::admin::UploadCleanupRebuildRequest::FromRequest(
            request,
            upload_id
        );
        if (!parsed) {
            Logger::Warn(log_context)
                << "Upload cleanup rebuild validation failed: " << parsed.error().message;
            co_return Response::Error(parsed.error());
        }
        log_context.upload_id = parsed->upload_id;

        auto result = co_await BuildService().RebuildUploadCleanup(
            parsed.value(),
            BuildAuditContext(request),
            log_context
        );
        if (!result) {
            Logger::Warn(log_context)
                << "Upload cleanup rebuild failed: " << result.error().message;
            co_return Response::Error(result.error());
        }

        log_context.state_version = result->state_version;
        log_context.job_id = result->job_id;
        Logger::Info(log_context)
            << "Upload cleanup rebuild successful: dry_run="
            << (result->dry_run ? "true" : "false");
        co_return Response::Success(result->ToJson());
    }

    auto StorageRecoveryAdminController::EnqueueReconciliation(
        drogon::HttpRequestPtr request,
        std::string scan_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Storage reconciliation enqueue request";

        auto parsed = disk::admin::StorageReconciliationEnqueueRequest::FromRequest(
            request,
            scan_id
        );
        if (!parsed) {
            Logger::Warn(log_context)
                << "Storage reconciliation enqueue validation failed: "
                << parsed.error().message;
            co_return Response::Error(parsed.error());
        }
        auto result = co_await BuildService().EnqueueReconciliation(
            parsed.value(),
            BuildAuditContext(request),
            log_context
        );
        if (!result) {
            Logger::Warn(log_context)
                << "Storage reconciliation enqueue failed: " << result.error().message;
            co_return Response::Error(result.error());
        }

        log_context.job_id = result->job_id;
        Logger::Info(log_context)
            << "Storage reconciliation enqueue successful: dry_run="
            << (result->dry_run ? "true" : "false");
        co_return Response::Success(result->ToJson());
    }

} // namespace disk::controllers
