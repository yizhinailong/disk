/**
 * @file StorageJobAdminController.cpp
 * @brief Administrator HTTP boundary for persistent storage jobs
 */

#include "controllers/StorageJobAdminController.hpp"

#include "dtos/StorageJobAdminDto.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/StorageJobAdminService.hpp"
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
        auto parsed = disk::admin::StorageJobListRequest::FromRequest(request);
        if (!parsed) {
            co_return Response::Error(parsed.error());
        }
        auto result = co_await BuildService().List(parsed.value());
        co_return result ? Response::Success(result->ToJson()) : Response::Error(result.error());
    }

    auto StorageJobAdminController::Get(
        drogon::HttpRequestPtr /*request*/,
        std::string id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto job_id = disk::admin::StorageJobReplayRequest::ParseJobId(id);
        if (!job_id) {
            co_return Response::Error(job_id.error());
        }
        auto result = co_await BuildService().Get(job_id.value());
        co_return result ? Response::Success(result->ToJson(true)) :
                           Response::Error(result.error());
    }

    auto StorageJobAdminController::Replay(
        drogon::HttpRequestPtr request,
        std::string id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto job_id = disk::admin::StorageJobReplayRequest::ParseJobId(id);
        if (!job_id) {
            co_return Response::Error(job_id.error());
        }
        auto parsed = disk::admin::StorageJobReplayRequest::FromRequest(
            request,
            job_id.value()
        );
        if (!parsed) {
            co_return Response::Error(parsed.error());
        }

        const disk::jobs::StorageJobAuditContext audit{
            .operator_id = request->attributes()->get<uint64_t>("user_id"),
            .ip_address = request->getPeerAddr().toIp(),
            .user_agent = request->getHeader("User-Agent"),
        };
        auto result = co_await BuildService().Replay(job_id.value(), parsed.value(), audit);
        co_return result ? Response::Success(result->ToJson()) : Response::Error(result.error());
    }

} // namespace disk::controllers
