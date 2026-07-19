/**
 * @file MetricsController.cpp
 * @brief Internal Prometheus metrics endpoint
 */

#include "controllers/MetricsController.hpp"

#include "services/MetricsService.hpp"
#include "services/ProcessRuntime.hpp"

namespace disk::controllers {

    auto MetricsController::Get(drogon::HttpRequestPtr /*request*/)
        -> drogon::Task<drogon::HttpResponsePtr> {
        disk::metrics::MetricsService service(
            drogon::app().getDbClient(),
            disk::runtime::ProcessRuntimeMgr::GetInstance()
        );
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setContentTypeString("text/plain; version=0.0.4; charset=utf-8");
        response->setBody(co_await service.Render());
        co_return response;
    }

} // namespace disk::controllers
