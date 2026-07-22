/**
 * @file MetricsController.cpp
 * @brief Internal Prometheus metrics endpoint
 */

#include "controllers/MetricsController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "services/MetricsService.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/ProcessRuntime.hpp"

namespace disk::controllers {

    auto MetricsController::Get(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        const auto log_context = GetRequestLogContext(request, "metrics");
        disk::metrics::MetricsService service(
            disk::metrics::ObserveDbClient(drogon::app().getDbClient()),
            disk::runtime::ProcessRuntimeMgr::GetInstance()
        );
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setContentTypeString("text/plain; version=0.0.4; charset=utf-8");
        response->setBody(co_await service.Render(log_context));
        co_return response;
    }

} // namespace disk::controllers
