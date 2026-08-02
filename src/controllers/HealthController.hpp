/**
 * @file HealthController.hpp
 * @brief liveness/readiness 控制器
 */

#pragma once

#include "services/HealthService.hpp"

namespace disk::health {

    class HealthController final : public drogon::HttpController<HealthController> {
    public:
        HealthController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(HealthController::Check, "/api/health", drogon::Get);
        ADD_METHOD_TO(HealthController::Live, "/api/health/live", drogon::Get);
        ADD_METHOD_TO(HealthController::Ready, "/api/health/ready", drogon::Get);
        METHOD_LIST_END

        [[nodiscard]]
        auto Check(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto Live(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto Ready(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<HealthService> m_health_service;
    };

} // namespace disk::health
