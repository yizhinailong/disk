/**
 * @file MetricsController.hpp
 * @brief Internal Prometheus metrics endpoint
 */

#pragma once

namespace disk::controllers {

    class MetricsController final : public drogon::HttpController<MetricsController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(MetricsController::Get, "/metrics", drogon::Get);
        METHOD_LIST_END

        [[nodiscard]] auto Get(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace disk::controllers
