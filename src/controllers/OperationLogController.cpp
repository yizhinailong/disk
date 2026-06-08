/**
 * @file OperationLogController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 操作日志控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "OperationLogController.hpp"

#include "utils/Response.hpp"

namespace disk::log {

    OperationLogController::OperationLogController()
        : m_log_service(std::make_unique<OperationLogService>(drogon::app().getDbClient())) {
    }

    auto OperationLogController::GetList(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received get operation logs request: " << request->getPeerAddr().toIpPort();

        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        int page = 1;
        int page_size = 20;

        auto page_str = request->getParameter("page");
        if (!page_str.empty()) {
            try {
                page = std::stoi(page_str);
                if (page < 1) {
                    page = 1;
                }
            } catch (...) {
                page = 1;
            }
        }

        auto page_size_str = request->getParameter("page_size");
        if (!page_size_str.empty()) {
            try {
                page_size = std::stoi(page_size_str);
                if (page_size < 1) {
                    page_size = 20;
                }
                if (page_size > 100) {
                    page_size = 100;
                }
            } catch (...) {
                page_size = 20;
            }
        }

        auto result = co_await m_log_service->GetList(user_id, page, page_size);
        if (!result) {
            Logger::Error() << "Failed to get operation logs: " << result.error().message;
            co_return Response::Error(result.error());
        }

        Json::Value items(Json::arrayValue);
        for (const auto& item : result->items) {
            items.append(item.ToJson());
        }

        Json::Value data;
        data["items"] = items;
        data["total"] = result->total;
        data["page"] = page;
        data["page_size"] = page_size;

        co_return Response::Success(data);
    }

} ///< namespace disk::log
