/**
 * @file OperationLogController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 操作日志控制器
 * @version 0.1
 * @date 2026-02-18
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/OperationLogService.hpp"

namespace disk::log {

    class OperationLogController : public drogon::HttpController<OperationLogController> {
    public:
        OperationLogController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            OperationLogController::GetList,
            "/api/logs",
            drogon::Get,
            "JwtAuthFilter",
            "RateLimitFilter"
        );
        METHOD_LIST_END

        [[nodiscard]]
        auto GetList(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<OperationLogService> m_log_service;
    };

} // namespace disk::log
