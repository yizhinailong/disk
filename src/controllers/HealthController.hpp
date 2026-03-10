/**
 * @file HealthController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 健康检查控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/HealthService.hpp"

namespace disk::health {

    // ==================== Controller ====================

    /**
     * @brief 健康检查控制器
     *
     * @details
     * 提供系统健康检查接口：
     * - GET /api/health - 系统健康检查（无需认证）
     *
     * 返回信息：
     * - overall_status: 整体状态 (healthy/degraded/unhealthy)
     * - components: 各组件状态 (database, redis)
     * - version: 系统版本
     * - uptime: 运行时间（秒）
     * - timestamp: 当前时间戳
     */
    class HealthController : public drogon::HttpController<HealthController> {
    public:
        HealthController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(HealthController::Check, "/api/health", drogon::Get);
        METHOD_LIST_END

        /**
         * @brief 健康检查
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Check(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<HealthService> m_health_service;
    };

} // namespace disk::health
