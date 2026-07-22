/**
 * @file FilterLogContext.hpp
 * @brief Explicit request correlation for HTTP filters
 */

#pragma once

#include <string>

#include <drogon/HttpRequest.h>

#include "services/MetricsService.hpp"
#include "utils/LogHelper.hpp"

namespace disk::filters {

    [[nodiscard]]
    inline auto GetFilterLogContext(
        const drogon::HttpRequestPtr& request
    ) -> disk::utils::LogContext {
        disk::utils::LogContext context;
        const auto attributes = request->attributes();
        if (attributes && attributes->find("request_id")) {
            context.request_id = attributes->get<std::string>("request_id");
        }
        context.operation = std::string(
            disk::metrics::HttpOperationName(
                disk::metrics::ClassifyHttpOperation(request->path())
            )
        );
        return context;
    }

} // namespace disk::filters
