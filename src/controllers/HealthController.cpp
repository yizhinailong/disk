/**
 * @file HealthController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 健康检查控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "HealthController.hpp"

#include "utils/Response.hpp"

namespace disk::health {

    HealthController::HealthController()
        : m_health_service(
              std::make_unique<HealthService>(
                  drogon::app().getDbClient(),
                  drogon::app().getRedisClient()
              )
          ) {
    }

    auto HealthController::Check(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        LOG_DEBUG << "Received health check request: " << request->getPeerAddr().toIpPort();

        auto health_result = co_await m_health_service->Check();

        auto data = health_result.ToJson();

        if (health_result.overall_status == "healthy") {
            co_return Response::Success(data);
        }

        // degraded 或 unhealthy 状态返回 503
        auto response = Response::Success(data);
        response->setStatusCode(drogon::k503ServiceUnavailable);
        co_return response;
    }

} // namespace disk::health
