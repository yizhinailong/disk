/**
 * @file StorageRecoveryAdminController.cpp
 * @brief Administrator HTTP boundary for audited storage recovery commands
 */

#include "controllers/StorageRecoveryAdminController.hpp"

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
        auto parsed = disk::admin::UploadLeaseReleaseRequest::FromRequest(
            request,
            upload_id
        );
        if (!parsed) {
            co_return Response::Error(parsed.error());
        }
        auto result = co_await BuildService().ReleaseUploadLease(
            parsed.value(),
            BuildAuditContext(request)
        );
        co_return result ? Response::Success(result->ToJson()) : Response::Error(result.error());
    }

    auto StorageRecoveryAdminController::RebuildUploadCleanup(
        drogon::HttpRequestPtr request,
        std::string upload_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto parsed = disk::admin::UploadCleanupRebuildRequest::FromRequest(
            request,
            upload_id
        );
        if (!parsed) {
            co_return Response::Error(parsed.error());
        }
        auto result = co_await BuildService().RebuildUploadCleanup(
            parsed.value(),
            BuildAuditContext(request)
        );
        co_return result ? Response::Success(result->ToJson()) : Response::Error(result.error());
    }

    auto StorageRecoveryAdminController::EnqueueReconciliation(
        drogon::HttpRequestPtr request,
        std::string scan_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto parsed = disk::admin::StorageReconciliationEnqueueRequest::FromRequest(
            request,
            scan_id
        );
        if (!parsed) {
            co_return Response::Error(parsed.error());
        }
        auto result = co_await BuildService().EnqueueReconciliation(
            parsed.value(),
            BuildAuditContext(request)
        );
        co_return result ? Response::Success(result->ToJson()) : Response::Error(result.error());
    }

} // namespace disk::controllers
