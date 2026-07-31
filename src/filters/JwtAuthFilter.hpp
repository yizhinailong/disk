/**
 * @file JwtAuthFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief   JWT 认证过滤器
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <drogon/HttpFilter.h>

#include "services/TokenService.hpp"

namespace disk::filters {
    class JwtAuthFilter : public drogon::HttpCoroFilter<JwtAuthFilter> {
    public:
        /**
         * @brief JWT认证过滤器
         * @param request HTTP请求
         * @return drogon::Task<drogon::HttpResponsePtr> 认证失败返回错误响应，成功返回nullptr继续链路
         */
        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;
    };
} // namespace disk::filters
