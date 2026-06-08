/**
 * @file AdminAuthFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief   管理员权限过滤器
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

namespace disk::filters {
    class AdminAuthFilter : public drogon::HttpCoroFilter<AdminAuthFilter> {
    public:
        /**
         * @brief 管理员权限过滤器
         * @param request HTTP请求
         * @return drogon::Task<drogon::HttpResponsePtr> 非管理员访问管理接口返回403，其余放行
         */
        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;
    };
} ///< namespace disk::filters
