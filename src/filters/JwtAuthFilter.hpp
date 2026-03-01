/**
 * @file JwtAuthFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief   JWT 认证过滤器
 * @version 0.1
 * @date 2026-01-17
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "services/TokenService.hpp"

namespace disk::filters {
    class JwtAuthFilter : public drogon::HttpCoroFilter<JwtAuthFilter> {
    public:
        JwtAuthFilter();

        /**
         * @brief JWT认证过滤器
         * @param request HTTP请求
         * @return drogon::Task<drogon::HttpResponsePtr> 认证失败返回错误响应，成功返回nullptr继续链路
         */
        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

    private:
    };
} // namespace disk::filters
