/**
 * @file HealthController.cpp
 * @brief liveness/readiness 控制器实现
 */

#include "controllers/HealthController.hpp"

#include <utility>

#include "services/ObservedDbClient.hpp"
#include "services/ProcessRuntime.hpp"
#include "storage/BlobStoreMgr.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/LogHelper.hpp"
#include "utils/Response.hpp"

namespace disk::health {

    HealthController::HealthController() {
        const auto runtime_state = disk::runtime::ProcessRuntimeMgr::GetInstance();
        drogon::nosql::RedisClientPtr redis_client;
        if (disk::utils::IncludesApi(runtime_state->Role())) {
            redis_client = drogon::app().getRedisClient();
        }
        m_health_service = std::make_unique<HealthService>(
            disk::metrics::ObserveDbClient(drogon::app().getDbClient()),
            std::move(redis_client),
            disk::storage::StorageMgr::GetUploadStagingStorage(),
            disk::storage::BlobStoreMgr::GetBlobStore(),
            std::move(runtime_state)
        );
    }

    auto HealthController::Check(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        co_return co_await Ready(std::move(request));
    }

    auto HealthController::Live(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        Logger::Debug() << "Received liveness request: " << request->getPeerAddr().toIpPort();
        co_return ToResponse(m_health_service->CheckLiveness());
    }

    auto HealthController::Ready(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        Logger::Debug() << "Received readiness request: " << request->getPeerAddr().toIpPort();
        co_return ToResponse(co_await m_health_service->CheckReadiness());
    }

    auto HealthController::ToResponse(const HealthResult& result) -> drogon::HttpResponsePtr {
        auto response = Response::Success(result.ToJson());
        if (result.overall_status != "healthy") {
            response->setStatusCode(drogon::k503ServiceUnavailable);
        }
        return response;
    }

} // namespace disk::health
