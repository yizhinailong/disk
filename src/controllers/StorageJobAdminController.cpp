/**
 * @file StorageJobAdminController.cpp
 * @brief Administrator HTTP boundary for persistent storage jobs
 */

#include "controllers/StorageJobAdminController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "dtos/StorageJobAdminDto.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/StorageJobAdminService.hpp"
#include "utils/ClientIp.hpp"
#include "utils/Response.hpp"

namespace disk::controllers {
    namespace {
        [[nodiscard]] auto BuildService() -> disk::jobs::StorageJobAdminService {
            return disk::jobs::StorageJobAdminService(
                disk::metrics::ObserveDbClient(drogon::app().getDbClient())
            );
        }
    } // namespace

    auto StorageJobAdminController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Storage job admin list request";

        auto parsed = disk::admin::StorageJobListRequest::FromRequest(request);
        if (!parsed) {
            Logger::Warn(log_context)
                << "Storage job admin list validation failed: " << parsed.error().message;
            co_return Response::Error(parsed.error());
        }
        auto result = co_await BuildService().List(parsed.value(), log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Storage job admin list failed: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Storage job admin list successful";
        co_return Response::Success(result->ToJson());
    }

    auto StorageJobAdminController::Get(
        drogon::HttpRequestPtr request,
        std::string id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Storage job admin detail request";

        auto job_id = disk::admin::StorageJobReplayRequest::ParseJobId(id);
        if (!job_id) {
            Logger::Warn(log_context)
                << "Storage job admin detail ID validation failed: " << job_id.error().message;
            co_return Response::Error(job_id.error());
        }
        log_context.job_id = job_id.value();

        auto result = co_await BuildService().Get(job_id.value(), log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Storage job admin detail failed: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context) << "Storage job admin detail successful";
        co_return Response::Success(result->ToJson(true));
    }

    auto StorageJobAdminController::Replay(
        drogon::HttpRequestPtr request,
        std::string id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = GetRequestLogContext(request, "admin");
        Logger::Info(log_context) << "Storage job replay request";

        auto job_id = disk::admin::StorageJobReplayRequest::ParseJobId(id);
        if (!job_id) {
            Logger::Warn(log_context)
                << "Storage job replay ID validation failed: " << job_id.error().message;
            co_return Response::Error(job_id.error());
        }
        log_context.job_id = job_id.value();

        auto parsed = disk::admin::StorageJobReplayRequest::FromRequest(
            request,
            job_id.value()
        );
        if (!parsed) {
            Logger::Warn(log_context)
                << "Storage job replay validation failed: " << parsed.error().message;
            co_return Response::Error(parsed.error());
        }

        const disk::jobs::StorageJobAuditContext audit{
            .operator_id = request->attributes()->get<uint64_t>("user_id"),
            .ip_address = disk::utils::ResolveClientIp(request),
            .user_agent = request->getHeader("User-Agent"),
        };
        auto result = co_await BuildService().Replay(
            job_id.value(),
            parsed.value(),
            audit,
            log_context
        );
        if (!result) {
            Logger::Warn(log_context)
                << "Storage job replay failed: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context)
            << "Storage job replay successful: dry_run="
            << (result->dry_run ? "true" : "false");
        co_return Response::Success(result->ToJson());
    }

} // namespace disk::controllers
